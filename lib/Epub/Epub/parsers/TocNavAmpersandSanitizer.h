#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Streaming fallback filter for the small subset of malformed XML commonly
// found in EPUB navigation documents: a bare ampersand in text or an
// attribute. It preserves the XML predefined entities and numeric references.
class TocNavAmpersandSanitizer final {
 public:
  using Sink = bool (*)(void* context, const uint8_t* data, size_t size);

  TocNavAmpersandSanitizer(Sink sink, void* context) : sink_(sink), context_(context) {}

  bool write(const uint8_t* data, size_t size);
  bool finish();
  bool changed() const { return changed_; }

 private:
  enum class CandidateType : uint8_t {
    None,
    NeedKind,
    Named,
    NumericPrefix,
    Decimal,
    Hex,
  };

  static constexpr size_t OUTPUT_BUFFER_SIZE = 1024;
  static constexpr size_t MAX_CANDIDATE_LENGTH = 64;

  Sink sink_;
  void* context_;
  std::array<uint8_t, OUTPUT_BUFFER_SIZE> output_{};
  size_t outputLength_ = 0;
  std::array<uint8_t, MAX_CANDIDATE_LENGTH> candidate_{};
  size_t candidateLength_ = 0;
  CandidateType candidateType_ = CandidateType::None;
  bool changed_ = false;
  bool finished_ = false;

  bool consume(uint8_t byte);
  bool emit(const uint8_t* data, size_t size);
  bool emitLiteral(const char* literal);
  bool flushOutput();
  bool appendCandidate(uint8_t byte);
  bool emitCandidateRaw();
  bool emitCandidateIfValid();
  bool isValidNamedEntity() const;
  void resetCandidate();
  static bool isAsciiLetter(uint8_t byte);
  static bool isDecimalDigit(uint8_t byte);
  static bool isHexDigit(uint8_t byte);
};
