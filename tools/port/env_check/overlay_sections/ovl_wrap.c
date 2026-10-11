/* What CMake generates per overlay source (AD-6): every global-storage variable goes to one
 * section per overlay, so __start_/__stop_ bracket the overlay's .data + .bss. */
#pragma clang attribute push(__attribute__((section("dw1ovl_btl"))), apply_to = variable(is_global))
#include "ovl_unit.c"
#pragma clang attribute pop
