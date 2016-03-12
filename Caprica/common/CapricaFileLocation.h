#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace caprica {

struct CapricaFileLocation final
{
  struct Partial final
  {
    size_t line{ };
    size_t column{ };

    explicit Partial(const CapricaFileLocation& loc) : line(loc.line), column(loc.column) { }
    Partial(const Partial&) = default;
    Partial(Partial&& other) = default;
    Partial& operator =(const Partial&) = default;
    Partial& operator =(Partial&&) = default;
    ~Partial() = default;

    CapricaFileLocation operator +(CapricaFileLocation loc) {
      loc.updatePartial(*this);
      return loc;
    }
  };

  std::string filename{ };
  size_t line{ 0 };
  size_t column{ 0 };

  explicit CapricaFileLocation(const std::string& fn, size_t ln, size_t col) noexcept : filename(fn), line(ln), column(col) { }
  CapricaFileLocation(const CapricaFileLocation& other) = default;
  CapricaFileLocation(CapricaFileLocation&& other) = default;
  CapricaFileLocation& operator =(const CapricaFileLocation&) = default;
  CapricaFileLocation& operator =(CapricaFileLocation&&) = default;
  ~CapricaFileLocation() = default;

  void nextLine() noexcept {
    line++;
    column = 0;
  }

  std::string buildString() const {
    std::ostringstream str;
    str << filename << "(" << line << "," << column << ")";
    return str.str();
  }

  void updatePartial(const Partial& part) noexcept {
    line = part.line;
    column = part.column;
  }
};

}
