#pragma once

#include <string>

#include <common/CapricaFileLocation.h>
#include <papyrus/PapyrusIdentifier.h>
#include <papyrus/PapyrusResolutionContext.h>
#include <papyrus/PapyrusType.h>
#include <papyrus/PapyrusValue.h>

#include <pex/PexFile.h>
#include <pex/PexFunction.h>
#include <pex/PexFunctionParameter.h>
#include <pex/PexObject.h>
#include <pex/PexState.h>

namespace caprica { namespace papyrus {

struct PapyrusFunctionParameter final
{
  std::string name{ "" };
  PapyrusType type;
  PapyrusValue defaultValue{ PapyrusValue::Default() };

  const CapricaFileLocation location;

  explicit PapyrusFunctionParameter(const CapricaFileLocation& loc, const PapyrusType& tp) : location(loc), type(tp) { }
  ~PapyrusFunctionParameter() = default;

  void buildPex(pex::PexFile* file, pex::PexObject* obj, pex::PexFunction* func) const {
    auto param = new pex::PexFunctionParameter();
    param->name = file->getString(name);
    param->type = type.buildPex(file);
    func->parameters.push_back(param);
  }
  
  void semantic(PapyrusResolutionContext* ctx) {
    type = ctx->resolveType(type);
    defaultValue = PapyrusResolutionContext::coerceDefaultValue(defaultValue, type);
  }
};

}}
