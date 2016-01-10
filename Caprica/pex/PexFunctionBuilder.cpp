#include <pex/PexFunctionBuilder.h>

namespace caprica { namespace pex {

PexFunctionBuilder& PexFunctionBuilder::operator <<(op::nop& instr) { return push(PexOpCode::Nop); }

#define OP_ARG1(name, opcode, destArgIdx, at1, an1) \
PexFunctionBuilder& PexFunctionBuilder::operator <<(op::name& instr) { return push(PexOpCode::opcode, instr.a1); }
#define OP_ARG2(name, opcode, destArgIdx, at1, an1, at2, an2) \
PexFunctionBuilder& PexFunctionBuilder::operator <<(op::name& instr) { return push(PexOpCode::opcode, instr.a1, instr.a2); }
#define OP_ARG3(name, opcode, destArgIdx, at1, an1, at2, an2, at3, an3) \
PexFunctionBuilder& PexFunctionBuilder::operator <<(op::name& instr) { return push(PexOpCode::opcode, instr.a1, instr.a2, instr.a3); }
#define OP_ARG4(name, opcode, destArgIdx, at1, an1, at2, an2, at3, an3, at4, an4) \
PexFunctionBuilder& PexFunctionBuilder::operator <<(op::name& instr) { return push(PexOpCode::opcode, instr.a1, instr.a2, instr.a3, instr.a4); }
#define OP_ARG5(name, opcode, destArgIdx, at1, an1, at2, an2, at3, an3, at4, an4, at5, an5) \
PexFunctionBuilder& PexFunctionBuilder::operator <<(op::name& instr) { return push(PexOpCode::opcode, instr.a1, instr.a2, instr.a3, instr.a4, instr.a5); }
OPCODES(OP_ARG1, OP_ARG2, OP_ARG3, OP_ARG4, OP_ARG5)
#undef OP_ARG1
#undef OP_ARG2
#undef OP_ARG3
#undef OP_ARG4
#undef OP_ARG5

PexFunctionBuilder& PexFunctionBuilder::operator <<(op::callmethod& instr) {
  return push(new PexInstruction(PexOpCode::CallMethod, std::vector<PexValue>{ instr.a1, instr.a2, instr.a3 }, instr.variadicArgs));
}
PexFunctionBuilder& PexFunctionBuilder::operator <<(op::callparent& instr) {
  return push(new PexInstruction(PexOpCode::CallParent, std::vector<PexValue>{ instr.a1, instr.a2 }, instr.variadicArgs));
}
PexFunctionBuilder& PexFunctionBuilder::operator <<(op::callstatic& instr) {
  return push(new PexInstruction(PexOpCode::CallStatic, std::vector<PexValue>{ instr.a1, instr.a2, instr.a3 }, instr.variadicArgs));
}

PexFunctionBuilder& PexFunctionBuilder::operator <<(const CapricaFileLocation& loc) {
  currentLocation = loc;
  return *this;
}

void PexFunctionBuilder::populateFunction(PexFunction* func, PexDebugFunctionInfo* debInfo) {
  for (size_t i = 0; i < instructions.size(); i++) {
    for (auto& arg : instructions[i]->args) {
      if (arg.type == PexValueType::Label) {
        if (arg.l->targetIdx == (size_t)-1)
          CapricaError::logicalFatal("Unresolved label!");
        auto newVal = arg.l->targetIdx - i;
        arg.type = PexValueType::Integer;
        arg.i = (int32_t)newVal;
      }
    }
  }

  for (auto l : labels) {
    if (l->targetIdx == (size_t)-1)
      CapricaError::logicalFatal("Unused unresolved label!");
    delete l;
  }
  labels.clear();

  for (auto tmp : tempVarRefs) {
    if (tmp->var == nullptr)
      CapricaError::logicalFatal("Unresolved tmp var!");
    delete tmp;
  }
  tempVarRefs.clear();

  func->instructions = instructions;
  func->locals = locals;
  debInfo->instructionLineMap.reserve(instructionLocations.size());
  for (auto& l : instructionLocations) {
    if (l.line > std::numeric_limits<uint16_t>::max())
      CapricaError::fatal(l, "The file has too many lines for the debug info to be able to map correctly!");
    debInfo->instructionLineMap.push_back((uint16_t)l.line);
  }
}

PexLocalVariable* PexFunctionBuilder::allocateLocal(const std::string& name, const papyrus::PapyrusType& tp) {
  auto loc = new PexLocalVariable();
  loc->name = file->getString(name);
  loc->type = tp.buildPex(file);
  locals.push_back(loc);
  return loc;
}

PexLocalVariable* PexFunctionBuilder::getNoneLocal(const CapricaFileLocation& location) {
  for (auto& loc : locals) {
    if (file->getStringValue(loc->name) == "::nonevar")
      return loc;
  }
  return allocateLocal("::nonevar", papyrus::PapyrusType::None(location));
}

PexValue::TemporaryVariable PexFunctionBuilder::allocTemp(const papyrus::PapyrusType& tp) {
  auto vRef = new PexTemporaryVariableRef(tp.buildPex(file));
  tempVarRefs.push_back(vRef);
  return PexValue::TemporaryVariable(vRef);
}

PexFunctionBuilder& PexFunctionBuilder::operator <<(PexLabel* loc) {
  loc->targetIdx = instructions.size();
  return *this;
}
PexFunctionBuilder& PexFunctionBuilder::operator >>(PexLabel*& loc) {
  loc = new PexLabel();
  labels.push_back(loc);
  return *this;
}

static int32_t getDestArgIndexForOpCode(PexOpCode op) {
  switch (op) {
    case PexOpCode::Nop:
      return -1;
    case PexOpCode::CallMethod:
    case PexOpCode::CallStatic:
      return 2;
    case PexOpCode::CallParent:
      return 1;
#define OP_ARG1(name, opcode, destArgIdx, at1, an1) \
    case PexOpCode::opcode: return destArgIdx;
#define OP_ARG2(name, opcode, destArgIdx, at1, an1, at2, an2) \
    case PexOpCode::opcode: return destArgIdx;
#define OP_ARG3(name, opcode, destArgIdx, at1, an1, at2, an2, at3, an3) \
    case PexOpCode::opcode: return destArgIdx;
#define OP_ARG4(name, opcode, destArgIdx, at1, an1, at2, an2, at3, an3, at4, an4) \
    case PexOpCode::opcode: return destArgIdx;
#define OP_ARG5(name, opcode, destArgIdx, at1, an1, at2, an2, at3, an3, at4, an4, at5, an5) \
    case PexOpCode::opcode: return destArgIdx;
      OPCODES(OP_ARG1, OP_ARG2, OP_ARG3, OP_ARG4, OP_ARG5)
#undef OP_ARG1
#undef OP_ARG2
#undef OP_ARG3
#undef OP_ARG4
#undef OP_ARG5
    default:
      CapricaError::logicalFatal("Unknown PexOpCode!");
  }
}

void PexFunctionBuilder::freeValueIfTemp(const PexValue& v) {
  PexString varName;
  if (v.type == PexValueType::Identifier)
    varName = v.s;
  else if (v.type == PexValueType::TemporaryVar && v.tmpVar->var)
    varName = v.tmpVar->var->name;
  else
    return;

  auto f = tempVarNameTypeMap.find(varName);
  if (f != tempVarNameTypeMap.end()) {
    if (!freeTempVars.count(f->second->type))
      freeTempVars.insert({ f->second->type, { } });
    freeTempVars[f->second->type].push_back(f->second);
  }
}

PexFunctionBuilder& PexFunctionBuilder::push(PexInstruction* instr) {
  for (auto& v : instr->args) {
    if (v.type == PexValueType::Invalid)
      CapricaError::fatal(currentLocation, "Attempted to use an invalid value as a value! (perhaps you tried to use the return value of a function that doesn't return?)");
    if (v.type == PexValueType::TemporaryVar && v.tmpVar->var)
      v = PexValue::Identifier(v.tmpVar->var);
    freeValueIfTemp(v);
  }
  for (auto& v : instr->variadicArgs) {
    if (v.type == PexValueType::Invalid)
      CapricaError::fatal(currentLocation, "Attempted to use an invalid value as a value! (perhaps you tried to use the return value of a function that doesn't return?)");
    if (v.type == PexValueType::TemporaryVar && v.tmpVar->var)
      v = PexValue::Identifier(v.tmpVar->var);
    freeValueIfTemp(v);
  }

  auto destIdx = getDestArgIndexForOpCode(instr->opCode);
  if (destIdx != -1 && instr->args[destIdx].type == PexValueType::TemporaryVar) {
    auto f = freeTempVars.find(instr->args[destIdx].tmpVar->type);
    if (f != freeTempVars.end() && f->second.size()) {
      PexLocalVariable* b = f->second.back();
      f->second.pop_back();
      instr->args[destIdx].tmpVar->var = b;
      instr->args[destIdx] = PexValue::Identifier(b->name);
    } else {
      auto loc = new PexLocalVariable();
      std::stringstream ss;
      ss << "::temp" << currentTempI++;
      loc->name = file->getString(ss.str());
      loc->type = instr->args[destIdx].tmpVar->type;
      tempVarNameTypeMap.insert({ loc->name, loc });
      locals.push_back(loc);
      instr->args[destIdx].tmpVar->var = loc;
      instr->args[destIdx] = PexValue::Identifier(loc);
    }
  }

  for (auto& v : instr->args) {
    if (v.type == PexValueType::TemporaryVar)
      CapricaError::fatal(currentLocation, "Attempted to use a temporary var before it's been assigned!");
  }
  for (auto& v : instr->variadicArgs) {
    if (v.type == PexValueType::TemporaryVar)
      CapricaError::fatal(currentLocation, "Attempted to use a temporary var before it's been assigned!");
  }

  // Special case for dead assigns.
  if (instr->opCode == PexOpCode::Assign) {
    auto dst = instr->args[0];
    auto src = instr->args[1];
    if (dst.type == src.type && dst.type == PexValueType::Identifier && dst.s.index == src.s.index)
      return *this;
  }

  instructionLocations.push_back(currentLocation);
  instructions.push_back(instr);
  return *this;
}

}}