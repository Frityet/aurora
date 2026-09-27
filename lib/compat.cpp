#include <cstddef>
#include <string>

extern "C" void _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE15_M_replace_coldEPcmPKcmm(
    std::string* text, char* position, std::size_t removed_length, const char* replacement,
    std::size_t replacement_length, std::size_t) {
  const auto offset = static_cast<std::size_t>(position - text->data());
  text->replace(offset, removed_length, replacement, replacement_length);
}
