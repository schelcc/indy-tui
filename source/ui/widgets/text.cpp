#include "ui/widget.hpp"
#include <mutex>

namespace UI {

void Text::append(UI::String &&s) noexcept {
  std::unique_lock lock(_mtx);
  text.append(std::move(s));
}

void Text::update(UI::String &&s) noexcept {
  std::unique_lock lock(_mtx);
  text = std::move(s);
}

void Text::clear() noexcept {
  std::unique_lock lock(_mtx);
  text = "";
}

void Text::apply(std::shared_ptr<ncpp::Plane> p) noexcept {
  std::shared_lock lock(_mtx);

  text.apply_to_plane(p, 1, 1);
}

}; // namespace UI
