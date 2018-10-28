#include <string>
template class std::basic_string<char>;

namespace std {
  void __throw_length_error(char const*) {
  }
  void __throw_logic_error(char const*) {
  }
}
