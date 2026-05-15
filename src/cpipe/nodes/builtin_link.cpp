// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

void cpipe_link_builtin_passthrough();
void cpipe_link_builtin_dng_input();
void cpipe_link_builtin_linearize_dng_lut();
void cpipe_link_builtin_blacklevel_dng_levels();
void cpipe_link_builtin_demosaic_bilinear();
void cpipe_link_builtin_wb_dual_illuminant();
void cpipe_link_builtin_colormatrix_dng_to_working();
void cpipe_link_builtin_precision_convert();
void cpipe_link_builtin_output_heif_sdr();

void cpipe_link_all_builtin_nodes() {
    cpipe_link_builtin_passthrough();
    cpipe_link_builtin_dng_input();
    cpipe_link_builtin_linearize_dng_lut();
    cpipe_link_builtin_blacklevel_dng_levels();
    cpipe_link_builtin_demosaic_bilinear();
    cpipe_link_builtin_wb_dual_illuminant();
    cpipe_link_builtin_colormatrix_dng_to_working();
    cpipe_link_builtin_precision_convert();
    cpipe_link_builtin_output_heif_sdr();
}
