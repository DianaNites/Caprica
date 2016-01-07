#pragma once

#include <cstring>
#include <memory>
#include <string>

#include <common/CapricaError.h>
#include <common/CapricaFileLocation.h>

#include <pex/PexFile.h>
#include <pex/PexString.h>

namespace caprica { namespace papyrus {

struct PapyrusObject;
struct PapyrusStruct;

struct PapyrusType final
{
  enum class Kind
  {
    None,
    Bool,
    Float,
    Int,
    String,
    Var,

    Array,
    Unresolved,
    ResolvedStruct,
    ResolvedObject,
  };

  // This is intended purely for initializing types that will be
  // assigned fully later in the control flow.
  struct Default final { };

  struct Unresolved final
  {
    CapricaFileLocation location;
    std::string name;

    Unresolved(const CapricaFileLocation& loc, const std::string& nm) : location(loc), name(nm) { }
    ~Unresolved() = default;
  };
  struct Array final
  {
    CapricaFileLocation location;
    std::shared_ptr<PapyrusType> type;

    Array(const CapricaFileLocation& loc, std::shared_ptr<PapyrusType> tp) : location(loc), type(tp) { }
    ~Array() = default;
  };
  struct None final
  {
    CapricaFileLocation location;

    None(const CapricaFileLocation& loc) : location(loc) { }
    ~None() = default;
  };
  struct Bool final
  {
    CapricaFileLocation location;

    Bool(const CapricaFileLocation& loc) : location(loc) { }
    ~Bool() = default;
  };
  struct Float final
  {
    CapricaFileLocation location;

    Float(const CapricaFileLocation& loc) : location(loc) { }
    ~Float() = default;
  };
  struct Int final
  {
    CapricaFileLocation location;

    Int(const CapricaFileLocation& loc) : location(loc) { }
    ~Int() = default;
  };
  struct String final
  {
    CapricaFileLocation location;

    String(const CapricaFileLocation& loc) : location(loc) { }
    ~String() = default;
  };
  struct Var final
  {
    CapricaFileLocation location;

    Var(const CapricaFileLocation& loc) : location(loc) { }
    ~Var() = default;
  };
  struct ResolvedObject final
  {
    CapricaFileLocation location;
    PapyrusObject* obj;

    ResolvedObject(const CapricaFileLocation& loc, PapyrusObject* o) : location(loc), obj(o) { }
    ~ResolvedObject() = default;
  };

  Kind type{ Kind::None };
  std::string name{ "" };
  CapricaFileLocation location;
  union
  {
    const PapyrusStruct* resolvedStruct{ nullptr };
    const PapyrusObject* resolvedObject;
  };

  PapyrusType(const Default& other) : type(Kind::Unresolved), location({ "", 0, 0 }) { }
  PapyrusType(const Unresolved& other) : type(Kind::Unresolved), name(other.name), location(other.location) { }
  PapyrusType(const Array& other) : type(Kind::Array), arrayElementType(other.type), location(other.location) { }
  PapyrusType(const None& other) : type(Kind::None), location(other.location) { }
  PapyrusType(const Bool& other) : type(Kind::Bool), location(other.location) { }
  PapyrusType(const Float& other) : type(Kind::Float), location(other.location) { }
  PapyrusType(const Int& other) : type(Kind::Int), location(other.location) { }
  PapyrusType(const String& other) : type(Kind::String), location(other.location) { }
  PapyrusType(const Var& other) : type(Kind::Var), location(other.location) { }
  PapyrusType(const ResolvedObject& other) : type(Kind::ResolvedObject), location(other.location), resolvedObject(other.obj) { }
  PapyrusType(const PapyrusType& other) = default;

  pex::PexString buildPex(pex::PexFile* file) const {
    return file->getString(getTypeString());
  }

  PapyrusType getElementType() const {
    assert(type == Kind::Array);
    return *arrayElementType;
  }

  bool operator ==(const PapyrusType& other) const {
    return !(*this != other);
  }

  bool operator !=(const PapyrusType& other) const {
    if (type == other.type) {
      switch (type) {
        case Kind::None:
        case Kind::Bool:
        case Kind::Float:
        case Kind::Int:
        case Kind::String:
        case Kind::Var:
          return false;
        case Kind::Array:
          return *arrayElementType != *other.arrayElementType;
        case Kind::Unresolved:
          return _stricmp(name.c_str(), other.name.c_str()) != 0;
        case Kind::ResolvedStruct:
          return resolvedStruct != other.resolvedStruct;
        case Kind::ResolvedObject:
          return resolvedObject != other.resolvedObject;
        default:
          CapricaError::logicalFatal("Unknown PapyrusTypeKind while comparing!");
      }
    }
    return true;
  }

private:
  std::string getTypeString() const;
  std::shared_ptr<PapyrusType> arrayElementType{ nullptr };
};

}}
