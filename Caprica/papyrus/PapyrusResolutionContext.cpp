#include <papyrus/PapyrusResolutionContext.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <boost/filesystem.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include <common/CapricaConfig.h>
#include <common/CapricaError.h>
#include <common/FSUtils.h>

#include <papyrus/PapyrusObject.h>
#include <papyrus/PapyrusScript.h>
#include <papyrus/PapyrusStruct.h>
#include <papyrus/expressions/PapyrusCastExpression.h>
#include <papyrus/expressions/PapyrusExpression.h>
#include <papyrus/expressions/PapyrusLiteralExpression.h>
#include <papyrus/parser/PapyrusParser.h>
#include <papyrus/statements/PapyrusDeclareStatement.h>

#include <pex/PexReflector.h>
#include <pex/parser/PexAsmParser.h>

namespace caprica { namespace papyrus {

void PapyrusResolutionContext::addImport(const CapricaFileLocation& location, const std::string& import) {
  auto sc = loadScript(import);
  if (!sc)
    CapricaError::error(location, "Failed to find imported script '%s'!", import.c_str());
  for (auto s : importedScripts) {
    if (s == sc)
      CapricaError::Warning::W4002_Duplicate_Import(location, import.c_str());
  }
  importedScripts.push_back(sc);
}

// This is safe because it will only ever contain scripts referencing items in this map, and this map
// will never contain a fully-resolved script.
static thread_local caseless_unordered_map<const std::string, std::unique_ptr<PapyrusScript>> loadedScripts{ };
static thread_local caseless_unordered_map<const std::string, caseless_unordered_map<const std::string, PapyrusScript*>> localPerDirIdentMap{ };
PapyrusScript* PapyrusResolutionContext::loadScript(const std::string& name) {
  auto baseDir = boost::filesystem::path(script->sourceFileName).parent_path().string();

  auto sf2 = localPerDirIdentMap.find(baseDir);
  if (sf2 != localPerDirIdentMap.end()) {
    auto sf3 = sf2->second.find(name);
    if (sf3 != sf2->second.end()) {
      return sf3->second;
    }
  }

  const auto searchDir = [](const std::string& baseDir, const std::string& scriptName) -> PapyrusScript* {
    const auto loadPsc = [](const std::string& scriptName, const std::string& baseDir, const std::string& filename) -> PapyrusScript* {
      auto f = loadedScripts.find(filename);
      if (f != loadedScripts.end())
        return f->second.get();

      // We should only ever be searching for things in the root import dir,
      // so this is safe.
      auto parser = new parser::PapyrusParser(filename);
      auto a = parser->parseScript();
      CapricaError::exitIfErrors();
      delete parser;
      if (!localPerDirIdentMap.count(baseDir))
        localPerDirIdentMap.insert({ baseDir,{ } });
      localPerDirIdentMap[baseDir].insert({ scriptName, a });
      loadedScripts.insert({ filename, std::unique_ptr<PapyrusScript>(a) });
      auto ctx = new PapyrusResolutionContext();
      ctx->resolvingReferenceScript = true;
      a->semantic(ctx);
      CapricaError::exitIfErrors();
      delete ctx;
      return a;
    };
    const auto loadPas = [](const std::string& scriptName, const std::string& baseDir, const std::string& filename) -> PapyrusScript* {
      auto f = loadedScripts.find(filename);
      if (f != loadedScripts.end())
        return f->second.get();

      auto parser = new pex::parser::PexAsmParser(filename);
      auto pex = parser->parseFile();
      CapricaError::exitIfErrors();
      delete parser;
      auto a = pex::PexReflector::reflectScript(pex);
      CapricaError::exitIfErrors();
      delete pex;
      if (!localPerDirIdentMap.count(baseDir))
        localPerDirIdentMap.insert({ baseDir,{ } });
      localPerDirIdentMap[baseDir].insert({ scriptName, a });
      loadedScripts.insert({ filename, std::unique_ptr<PapyrusScript>(a) });
      auto ctx = new PapyrusResolutionContext();
      ctx->resolvingReferenceScript = true;
      ctx->isPexResolution = true;
      a->semantic(ctx);
      CapricaError::exitIfErrors();
      delete ctx;
      return a;
    };
    const auto loadPex = [](const std::string& scriptName, const std::string& baseDir, const std::string& filename) -> PapyrusScript* {
      auto f = loadedScripts.find(filename);
      if (f != loadedScripts.end())
        return f->second.get();

      pex::PexReader rdr(filename);
      auto pex = pex::PexFile::read(rdr);
      CapricaError::exitIfErrors();
      auto a = pex::PexReflector::reflectScript(pex);
      CapricaError::exitIfErrors();
      delete pex;
      if (!localPerDirIdentMap.count(baseDir))
        localPerDirIdentMap.insert({ baseDir,{ } });
      localPerDirIdentMap[baseDir].insert({ scriptName, a });
      loadedScripts.insert({ filename, std::unique_ptr<PapyrusScript>(a) });
      auto ctx = new PapyrusResolutionContext();
      ctx->resolvingReferenceScript = true;
      ctx->isPexResolution = true;
      a->semantic(ctx);
      CapricaError::exitIfErrors();
      delete ctx;
      return a;
    };
    const auto normalizeDir = [](const std::string& filename) -> std::string {
      return FSUtils::canonical(filename).string();
    };

    if (boost::filesystem::exists(baseDir + "\\" + scriptName + ".psc"))
      return loadPsc(scriptName, baseDir, normalizeDir(baseDir + "\\" + scriptName + ".psc"));
    else if (boost::filesystem::exists(baseDir + "\\" + scriptName + ".pas"))
      return loadPas(scriptName, baseDir, normalizeDir(baseDir + "\\" + scriptName + ".pas"));
    else if (boost::filesystem::exists(baseDir + "\\" + scriptName + ".pex"))
      return loadPex(scriptName, baseDir, normalizeDir(baseDir + "\\" + scriptName + ".pex"));

    return nullptr;
  };

  // Allow references to subdirs.
  auto nm2 = name;
  std::replace(nm2.begin(), nm2.end(), ':', '\\');
  if (auto s = searchDir(baseDir, nm2))
    return s;

  for (auto& dir : CapricaConfig::importDirectories) {
    if (auto s = searchDir(dir, nm2))
      return s;
  }

  return nullptr;
}

bool PapyrusResolutionContext::isObjectSomeParentOf(const PapyrusObject* child, const PapyrusObject* parent) {
  if (child == parent)
    return true;
  if (!_stricmp(child->name.c_str(), parent->name.c_str()))
    return true;
  if (auto parentObject = child->tryGetParentClass())
    return isObjectSomeParentOf(parentObject, parent);
  return false;
}

bool PapyrusResolutionContext::canExplicitlyCast(const PapyrusType& src, const PapyrusType& dest) {
  if (canImplicitlyCoerce(src, dest))
    return true;

  if (src.type == PapyrusType::Kind::Var)
    return dest.type != PapyrusType::Kind::None;

  switch (dest.type) {
    case PapyrusType::Kind::Int:
    case PapyrusType::Kind::Float:
      switch (src.type) {
        case PapyrusType::Kind::String:
        case PapyrusType::Kind::Int:
        case PapyrusType::Kind::Float:
        case PapyrusType::Kind::Bool:
        case PapyrusType::Kind::Var:
          return true;
        default:
          return false;
      }

    case PapyrusType::Kind::ResolvedObject:
      if (src.type == PapyrusType::Kind::ResolvedObject)
        return isObjectSomeParentOf(dest.resolvedObject, src.resolvedObject);
      return false;
    case PapyrusType::Kind::Array:
      if (src.type == PapyrusType::Kind::Array && src.getElementType().type == PapyrusType::Kind::ResolvedObject && dest.getElementType().type == PapyrusType::Kind::ResolvedObject) {
        return isObjectSomeParentOf(dest.getElementType().resolvedObject, src.getElementType().resolvedObject);
      }
      return false;

    case PapyrusType::Kind::None:
    case PapyrusType::Kind::Bool:
    case PapyrusType::Kind::String:
    case PapyrusType::Kind::Var:
    case PapyrusType::Kind::Unresolved:
    case PapyrusType::Kind::ResolvedStruct:
      return false;

    default:
      CapricaError::logicalFatal("Unknown PapyrusTypeKind!");
  }
}

bool PapyrusResolutionContext::canImplicitlyCoerce(const PapyrusType& src, const PapyrusType& dest) {
  if (src == dest)
    return true;

  switch (dest.type) {
    case PapyrusType::Kind::Bool:
      return src.type != PapyrusType::Kind::None;
    case PapyrusType::Kind::Float:
      return src.type == PapyrusType::Kind::Int;
    case PapyrusType::Kind::String:
      return src.type != PapyrusType::Kind::None;
    case PapyrusType::Kind::ResolvedObject:
      if (src.type == PapyrusType::Kind::ResolvedObject)
        return isObjectSomeParentOf(src.resolvedObject, dest.resolvedObject);
      return false;
    case PapyrusType::Kind::Var:
      return src.type != PapyrusType::Kind::None;
    case PapyrusType::Kind::None:
    case PapyrusType::Kind::Int:
    case PapyrusType::Kind::Array:
    case PapyrusType::Kind::Unresolved:
    case PapyrusType::Kind::ResolvedStruct:
      return false;
    default:
      CapricaError::logicalFatal("Unknown PapyrusTypeKind!");
  }
}

bool PapyrusResolutionContext::canImplicitlyCoerceExpression(expressions::PapyrusExpression* expr, const PapyrusType& target) {
  bool canCast = canImplicitlyCoerce(expr->resultType(), target);
  switch (target.type) {
    case PapyrusType::Kind::Bool:
    case PapyrusType::Kind::Int:
    case PapyrusType::Kind::Float:
    case PapyrusType::Kind::String:
    case PapyrusType::Kind::Unresolved:
      break;

    // Implicit conversion from None to each of these is allowed, but only for a literal None
    case PapyrusType::Kind::Var:
    case PapyrusType::Kind::Array:
    case PapyrusType::Kind::ResolvedObject:
    case PapyrusType::Kind::ResolvedStruct:
      if (expr->resultType().type == PapyrusType::Kind::None && expr->is<expressions::PapyrusLiteralExpression>())
        canCast = true;
      break;
    default:
      CapricaError::logicalFatal("Unknown PapyrusTypeKind!");
  }
  return canCast;
}

expressions::PapyrusExpression* PapyrusResolutionContext::coerceExpression(expressions::PapyrusExpression* expr, const PapyrusType& target) {
  if (expr->resultType() != target) {
    bool canCast = canImplicitlyCoerceExpression(expr, target);

    if (canCast && expr->resultType().type == PapyrusType::Kind::Int && target.type == PapyrusType::Kind::Float) {
      if (auto le = expr->as<expressions::PapyrusLiteralExpression>()) {
        le->value.f = (float)le->value.i;
        le->value.type = PapyrusValueType::Float;
        return expr;
      }
    }

    if (!canCast) {
      CapricaError::error(expr->location, "No implicit conversion from '%s' to '%s' exists!", expr->resultType().prettyString().c_str(), target.prettyString().c_str());
      return expr;
    }
    auto ce = new expressions::PapyrusCastExpression(expr->location, target);
    ce->innerExpression = expr;
    return ce;
  }
  return expr;
}

PapyrusValue PapyrusResolutionContext::coerceDefaultValue(const PapyrusValue& val, const PapyrusType& target) {
  if (val.type == PapyrusValueType::Invalid || val.getPapyrusType() == target)
    return val;

  switch (target.type) {
    case PapyrusType::Kind::Float:
      if (val.getPapyrusType().type == PapyrusType::Kind::Int && target.type == PapyrusType::Kind::Float)
        return PapyrusValue::Float(val.location, (float)val.i);
      break;
    case PapyrusType::Kind::Array:
    case PapyrusType::Kind::ResolvedObject:
    case PapyrusType::Kind::ResolvedStruct:
      if (val.getPapyrusType().type == PapyrusType::Kind::None)
        return val;
      break;
    default:
      break;
  }
  CapricaError::error(val.location, "Cannot initialize a '%s' value with a '%s'!", target.prettyString().c_str(), val.getPapyrusType().prettyString().c_str());
  return val;
}

PapyrusState* PapyrusResolutionContext::tryResolveState(const std::string& name, const PapyrusObject* parentObj) const {
  if (!parentObj)
    parentObj = object;

  for (auto s : parentObj->states) {
    if (!_stricmp(s->name.c_str(), name.c_str()))
      return s;
  }

  if (auto parentClass = parentObj->tryGetParentClass())
    return tryResolveState(name, parentClass);

  return nullptr;
}

PapyrusType PapyrusResolutionContext::resolveType(PapyrusType tp) {
  if (tp.type != PapyrusType::Kind::Unresolved) {
    if (tp.type == PapyrusType::Kind::Array)
      return PapyrusType::Array(tp.location, std::make_shared<PapyrusType>(resolveType(tp.getElementType())));
    return tp;
  }

  if (isPexResolution || CapricaConfig::allowDecompiledStructNameRefs) {
    auto pos = tp.name.find_first_of('#');
    if (pos != std::string::npos) {
      auto scName = tp.name.substr(0, pos);
      auto strucName = tp.name.substr(pos + 1);
      auto sc = loadScript(scName);
      if (!sc)
        CapricaError::fatal(tp.location, "Unable to find script '%s' referenced by '%s'!", scName.c_str(), tp.name.c_str());

      for (auto obj : sc->objects) {
        for (auto struc : obj->structs) {
          if (!_stricmp(struc->name.c_str(), strucName.c_str())) {
            tp.type = PapyrusType::Kind::ResolvedStruct;
            tp.resolvedStruct = struc;
            return tp;
          }
        }
      }

      CapricaError::fatal(tp.location, "Unable to resolve a struct named '%s' in script '%s'!", strucName.c_str(), scName.c_str());
    }
  }

  if (object) {
    for (auto& s : object->structs) {
      if (!_stricmp(s->name.c_str(), tp.name.c_str())) {
        tp.type = PapyrusType::Kind::ResolvedStruct;
        tp.resolvedStruct = s;
        return tp;
      }
    }

    if (!_stricmp(object->name.c_str(), tp.name.c_str())) {
      tp.type = PapyrusType::Kind::ResolvedObject;
      tp.resolvedObject = object;
      return tp;
    }
  }

  for (auto sc : importedScripts) {
    for (auto obj : sc->objects) {
      for (auto struc : obj->structs) {
        if (!_stricmp(struc->name.c_str(), tp.name.c_str())) {
          tp.type = PapyrusType::Kind::ResolvedStruct;
          tp.resolvedStruct = struc;
          return tp;
        }
      }
    }
  }

  auto sc = loadScript(tp.name);
  if (sc != nullptr) {
    for (auto obj : sc->objects) {
      if (!_stricmp(obj->name.c_str(), tp.name.c_str())) {
        tp.type = PapyrusType::Kind::ResolvedObject;
        tp.resolvedObject = obj;
        return tp;
      }
    }
  }
  
  CapricaError::fatal(tp.location, "Unable to resolve type '%s'!", tp.name.c_str());
}

void PapyrusResolutionContext::addLocalVariable(statements::PapyrusDeclareStatement* local) {
  for (auto is : localVariableScopeStack) {
    if (is.count(local->name)) {
      CapricaError::error(local->location, "Attempted to redefined '%s' which was already defined in a parent scope!", local->name.c_str());
      return;
    }
  }
  localVariableScopeStack.back().insert({ local->name, local });
}

PapyrusIdentifier PapyrusResolutionContext::resolveIdentifier(const PapyrusIdentifier& ident) const {
  auto id = tryResolveIdentifier(ident);
  if (id.type == PapyrusIdentifierType::Unresolved)
    CapricaError::fatal(ident.location, "Unresolved identifier '%s'!", ident.name.c_str());
  return id;
}

PapyrusIdentifier PapyrusResolutionContext::tryResolveIdentifier(const PapyrusIdentifier& ident) const {
  if (ident.type != PapyrusIdentifierType::Unresolved)
    return ident;

  // This handles local var resolution.
  for (auto& stack : boost::adaptors::reverse(localVariableScopeStack)) {
    auto f = stack.find(ident.name);
    if (f != stack.end()) {
      return PapyrusIdentifier::DeclStatement(ident.location, f->second);
    }
  }

  if (function) {
    for (auto p : function->parameters) {
      if (!_stricmp(p->name.c_str(), ident.name.c_str()))
        return PapyrusIdentifier::FunctionParameter(ident.location, p);
    }
  }

  if (!function || !function->isGlobal) {
    for (auto v : object->variables) {
      if (!_stricmp(v->name.c_str(), ident.name.c_str()))
        return PapyrusIdentifier::Variable(ident.location, v);
    }

    for (auto pg : object->propertyGroups) {
      for (auto p : pg->properties) {
        if (!_stricmp(p->name.c_str(), ident.name.c_str()))
          return PapyrusIdentifier::Property(ident.location, p);
      }
    }
  }

  if (auto parentClass = object->tryGetParentClass())
    return tryResolveMemberIdentifier(object->parentClass, ident);

  return ident;
}

PapyrusIdentifier PapyrusResolutionContext::resolveMemberIdentifier(const PapyrusType& baseType, const PapyrusIdentifier& ident) const {
  auto id = tryResolveMemberIdentifier(baseType, ident);
  if (id.type == PapyrusIdentifierType::Unresolved)
    CapricaError::fatal(ident.location, "Unresolved identifier '%s'!", ident.name.c_str());
  return id;
}

PapyrusIdentifier PapyrusResolutionContext::tryResolveMemberIdentifier(const PapyrusType& baseType, const PapyrusIdentifier& ident) const {
  if (ident.type != PapyrusIdentifierType::Unresolved)
    return ident;

  if (baseType.type == PapyrusType::Kind::ResolvedStruct) {
    for (auto& sm : baseType.resolvedStruct->members) {
      if (!_stricmp(sm->name.c_str(), ident.name.c_str()))
        return PapyrusIdentifier::StructMember(ident.location, sm);
    }
  } else if (baseType.type == PapyrusType::Kind::ResolvedObject) {
    for (auto& propGroup : baseType.resolvedObject->propertyGroups) {
      for (auto& prop : propGroup->properties) {
        if (!_stricmp(prop->name.c_str(), ident.name.c_str()))
          return PapyrusIdentifier::Property(ident.location, prop);
      }
    }

    if (auto parentClass = baseType.resolvedObject->tryGetParentClass())
      return tryResolveMemberIdentifier(baseType.resolvedObject->parentClass, ident);
  }

  return ident;
}

PapyrusIdentifier PapyrusResolutionContext::resolveFunctionIdentifier(const PapyrusType& baseType, const PapyrusIdentifier& ident) const {
  auto id = tryResolveFunctionIdentifier(baseType, ident);
  if (id.type == PapyrusIdentifierType::Unresolved)
    CapricaError::fatal(ident.location, "Unresolved function name '%s'!", ident.name.c_str());
  return id;
}

PapyrusIdentifier PapyrusResolutionContext::tryResolveFunctionIdentifier(const PapyrusType& baseType, const PapyrusIdentifier& ident) const {
  if (ident.type != PapyrusIdentifierType::Unresolved)
    return ident;

  if (baseType.type == PapyrusType::Kind::None) {
    if (auto state = object->tryGetRootState()) {
      for (auto& func : state->functions) {
        if (!_stricmp(func->name.c_str(), ident.name.c_str())) {
          if (function && function->isGlobal && !func->isGlobal)
            CapricaError::error(ident.location, "You cannot call non-global functions from within a global function. '%s' is not a global function.", func->name.c_str());
          return PapyrusIdentifier::Function(ident.location, func);
        }
      }
    }

    for (auto sc : importedScripts) {
      for (auto obj : sc->objects) {
        if (auto state = obj->tryGetRootState()) {
          for (auto& func : state->functions) {
            if (func->isGlobal && !_stricmp(func->name.c_str(), ident.name.c_str()))
              return PapyrusIdentifier::Function(ident.location, func);
          }
        }
      }
    }

    return resolveFunctionIdentifier(PapyrusType::ResolvedObject(ident.location, object), ident);
  } else if (baseType.type == PapyrusType::Kind::Array) {
    auto fk = PapyrusBuiltinArrayFunctionKind::Unknown;
    if (!_stricmp(ident.name.c_str(), "find")) {
      if (baseType.getElementType().type == PapyrusType::Kind::ResolvedStruct)
        fk = PapyrusBuiltinArrayFunctionKind::FindStruct;
      else
        fk = PapyrusBuiltinArrayFunctionKind::Find;
    } else if (!_stricmp(ident.name.c_str(), "rfind")) {
      if (baseType.getElementType().type == PapyrusType::Kind::ResolvedStruct)
        fk = PapyrusBuiltinArrayFunctionKind::RFindStruct;
      else
        fk = PapyrusBuiltinArrayFunctionKind::RFind;
    } else if (!_stricmp(ident.name.c_str(), "add")) {
      fk = PapyrusBuiltinArrayFunctionKind::Add;
    } else if (!_stricmp(ident.name.c_str(), "clear")) {
      fk = PapyrusBuiltinArrayFunctionKind::Clear;
    } else if (!_stricmp(ident.name.c_str(), "insert")) {
      fk = PapyrusBuiltinArrayFunctionKind::Insert;
    } else if (!_stricmp(ident.name.c_str(), "remove")) {
      fk = PapyrusBuiltinArrayFunctionKind::Remove;
    } else if (!_stricmp(ident.name.c_str(), "removelast")) {
      fk = PapyrusBuiltinArrayFunctionKind::RemoveLast;
    } else {
      CapricaError::fatal(ident.location, "Unknown function '%s' called on an array expression!", ident.name.c_str());
    }
    return PapyrusIdentifier::ArrayFunction(baseType.location, fk, baseType.getElementType());
  } else if (baseType.type == PapyrusType::Kind::ResolvedObject) {
    if (auto state = baseType.resolvedObject->tryGetRootState()) {
      for (auto& func : state->functions) {
        if (!_stricmp(func->name.c_str(), ident.name.c_str())) {
          if (func->isGlobal)
            CapricaError::error(ident.location, "You cannot call the global function '%s' on an object.", func->name.c_str());
          return PapyrusIdentifier::Function(ident.location, func);
        }
      }
    }

    if (auto parentClass = baseType.resolvedObject->tryGetParentClass())
      return resolveFunctionIdentifier(baseType.resolvedObject->parentClass, ident);
  }

  return ident;
}

}}
