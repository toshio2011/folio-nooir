#include "TocNavAmpersandSanitizer.h"

#include <cstring>
#include <string_view>

namespace {
constexpr std::string_view NAMED_ENTITIES[] = {"amp", "lt", "gt", "quot", "apos"};
}

bool TocNavAmpersandSanitizer::isAsciiLetter(const uint8_t byte) {
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

bool TocNavAmpersandSanitizer::isDecimalDigit(const uint8_t byte) { return byte >= '0' && byte <= '9'; }

bool TocNavAmpersandSanitizer::isHexDigit(const uint8_t byte) {
  return isDecimalDigit(byte) || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F');
}

bool TocNavAmpersandSanitizer::flushOutput() {
  if (outputLength_ == 0) return true;
  if (!sink_ || !sink_(context_, output_.data(), outputLength_)) return false;
  outputLength_ = 0;
  return true;
}

bool TocNavAmpersandSanitizer::emit(const uint8_t* data, const size_t size) {
  size_t offset = 0;
  while (offset < size) {
    const size_t available = OUTPUT_BUFFER_SIZE - outputLength_;
    const size_t copyLength = (size - offset < available) ? size - offset : available;
    memcpy(output_.data() + outputLength_, data + offset, copyLength);
    outputLength_ += copyLength;
    offset += copyLength;
    if (outputLength_ == OUTPUT_BUFFER_SIZE && !flushOutput()) return false;
  }
  return true;
}

bool TocNavAmpersandSanitizer::emitLiteral(const char* literal) {
  return emit(reinterpret_cast<const uint8_t*>(literal), strlen(literal));
}

void TocNavAmpersandSanitizer::resetCandidate() {
  candidateLength_ = 0;
  candidateType_ = CandidateType::None;
}

bool TocNavAmpersandSanitizer::appendCandidate(const uint8_t byte) {
  if (candidateLength_ >= MAX_CANDIDATE_LENGTH) {
    if (!emitCandidateIfValid()) return false;
    resetCandidate();
    return consume(byte);
  }
  candidate_[candidateLength_++] = byte;
  return true;
}

bool TocNavAmpersandSanitizer::emitCandidateRaw() {
  return emit(candidate_.data(), candidateLength_);
}

bool TocNavAmpersandSanitizer::isValidNamedEntity() const {
  if (candidateLength_ < 3 || candidate_[candidateLength_ - 1] != ';') return false;
  const std::string_view name(reinterpret_cast<const char*>(candidate_.data() + 1), candidateLength_ - 2);
  for (const auto validName : NAMED_ENTITIES) {
    if (name == validName) return true;
  }
  return false;
}

bool TocNavAmpersandSanitizer::emitCandidateIfValid() {
  bool valid = false;
  if (candidateType_ == CandidateType::Named) {
    valid = isValidNamedEntity();
  } else if (candidateType_ == CandidateType::Decimal && candidateLength_ >= 4 &&
             candidate_[candidateLength_ - 1] == ';') {
    valid = true;
    for (size_t i = 2; i + 1 < candidateLength_; ++i) {
      if (!isDecimalDigit(candidate_[i])) {
        valid = false;
        break;
      }
    }
  } else if (candidateType_ == CandidateType::Hex && candidateLength_ >= 5 &&
             candidate_[candidateLength_ - 1] == ';') {
    valid = true;
    for (size_t i = 3; i + 1 < candidateLength_; ++i) {
      if (!isHexDigit(candidate_[i])) {
        valid = false;
        break;
      }
    }
  }

  if (valid) return emitCandidateRaw();

  // The candidate is not a valid XML reference, so preserve its visible text
  // while making only the leading ampersand legal XML.
  changed_ = true;
  if (!emitLiteral("&amp;")) return false;
  return candidateLength_ <= 1 || emit(candidate_.data() + 1, candidateLength_ - 1);
}

bool TocNavAmpersandSanitizer::consume(const uint8_t byte) {
  if (candidateType_ == CandidateType::None) {
    if (byte != '&') return emit(&byte, 1);
    candidate_[0] = '&';
    candidateLength_ = 1;
    candidateType_ = CandidateType::NeedKind;
    return true;
  }

  if (candidateType_ == CandidateType::NeedKind) {
    if (byte == '#') {
      candidateType_ = CandidateType::NumericPrefix;
      return appendCandidate(byte);
    }
    if (isAsciiLetter(byte)) {
      candidateType_ = CandidateType::Named;
      return appendCandidate(byte);
    }

    // A non-name/non-numeric character immediately after '&' makes this a
    // truly bare ampersand, as in "Terpecah & Terbelah".
    changed_ = true;
    if (!emitLiteral("&amp;")) return false;
    resetCandidate();
    return consume(byte);
  }

  if (candidateType_ == CandidateType::Named) {
    if (byte == ';') {
      if (!appendCandidate(byte)) return false;
      const bool ok = emitCandidateIfValid();
      resetCandidate();
      return ok;
    }
    if (isAsciiLetter(byte)) return appendCandidate(byte);

    if (!emitCandidateIfValid()) return false;
    resetCandidate();
    return consume(byte);
  }

  if (candidateType_ == CandidateType::NumericPrefix) {
    if (byte == 'x' || byte == 'X') {
      candidateType_ = CandidateType::Hex;
      return appendCandidate(byte);
    }
    if (isDecimalDigit(byte)) {
      candidateType_ = CandidateType::Decimal;
      return appendCandidate(byte);
    }
    if (!emitCandidateIfValid()) return false;
    resetCandidate();
    return consume(byte);
  }

  if (candidateType_ == CandidateType::Decimal) {
    if (isDecimalDigit(byte)) return appendCandidate(byte);
    if (byte == ';') {
      if (!appendCandidate(byte)) return false;
      const bool ok = emitCandidateIfValid();
      resetCandidate();
      return ok;
    }
    if (!emitCandidateIfValid()) return false;
    resetCandidate();
    return consume(byte);
  }

  // Hexadecimal numeric reference.
  if (isHexDigit(byte)) return appendCandidate(byte);
  if (byte == ';') {
    if (!appendCandidate(byte)) return false;
    const bool ok = emitCandidateIfValid();
    resetCandidate();
    return ok;
  }
  if (!emitCandidateIfValid()) return false;
  resetCandidate();
  return consume(byte);
}

bool TocNavAmpersandSanitizer::write(const uint8_t* data, const size_t size) {
  if (finished_ || (!data && size != 0)) return false;
  for (size_t i = 0; i < size; ++i) {
    if (!consume(data[i])) return false;
  }
  return true;
}

bool TocNavAmpersandSanitizer::finish() {
  if (finished_) return true;
  finished_ = true;

  if (candidateType_ != CandidateType::None) {
    if (candidateLength_ == 1) {
      changed_ = true;
      if (!emitLiteral("&amp;")) return false;
    } else if (!emitCandidateIfValid()) {
      return false;
    }
    resetCandidate();
  }

  return flushOutput();
}
