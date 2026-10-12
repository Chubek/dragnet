#pragma once
#include "object.h"
#include "bin2bin/object.hpp"
struct limestone_object_target { limestone::bin2bin::ObjectTarget target; };
struct limestone_object {
  limestone::bin2bin::ObjectFile file;
  std::string text;
  explicit limestone_object(limestone::bin2bin::ObjectFile input):file(std::move(input)),text(limestone::bin2bin::print_object(file)){}
};
