// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 cpipe contributors

void cpipe_link_builtin_passthrough();
void cpipe_link_builtin_dng_input();
void cpipe_link_builtin_linearize_dng_lut();
void cpipe_link_builtin_blacklevel_dng_levels();
void cpipe_link_builtin_demosaic_amaze();
void cpipe_link_builtin_demosaic_bilinear();
void cpipe_link_builtin_demosaic_quad_bayer_remosaic();
void cpipe_link_builtin_demosaic_rcd();
void cpipe_link_builtin_wb_dual_illuminant();
void cpipe_link_builtin_wb_greyworld_auto();
void cpipe_link_builtin_colormatrix_dng_to_working();
void cpipe_link_builtin_denoise_bm3d();
void cpipe_link_builtin_denoise_guided_filter();
void cpipe_link_builtin_denoise_wavelet_bayes_shrink();
void cpipe_link_builtin_color_3d_lut();
void cpipe_link_builtin_color_scene_linear_to_display();
void cpipe_link_builtin_sharpen_edge_aware_usm();
void cpipe_link_builtin_tone_aces_filmic();
void cpipe_link_builtin_tone_filmic_rgb();
void cpipe_link_builtin_tone_mertens_local();
void cpipe_link_builtin_tone_reinhard();
void cpipe_link_builtin_lens_shading_gainmap();
void cpipe_link_builtin_lens_dng_opcode_list_3();
void cpipe_link_builtin_fusion_hdr_plus_derivative();
void cpipe_link_builtin_precision_convert();
void cpipe_link_builtin_output_heif_sdr();

void cpipe_link_all_builtin_nodes() {
    cpipe_link_builtin_passthrough();
    cpipe_link_builtin_dng_input();
    cpipe_link_builtin_linearize_dng_lut();
    cpipe_link_builtin_blacklevel_dng_levels();
    cpipe_link_builtin_demosaic_amaze();
    cpipe_link_builtin_demosaic_bilinear();
    cpipe_link_builtin_demosaic_quad_bayer_remosaic();
    cpipe_link_builtin_demosaic_rcd();
    cpipe_link_builtin_wb_dual_illuminant();
    cpipe_link_builtin_wb_greyworld_auto();
    cpipe_link_builtin_colormatrix_dng_to_working();
    cpipe_link_builtin_denoise_bm3d();
    cpipe_link_builtin_denoise_guided_filter();
    cpipe_link_builtin_denoise_wavelet_bayes_shrink();
    cpipe_link_builtin_color_3d_lut();
    cpipe_link_builtin_color_scene_linear_to_display();
    cpipe_link_builtin_sharpen_edge_aware_usm();
    cpipe_link_builtin_tone_aces_filmic();
    cpipe_link_builtin_tone_filmic_rgb();
    cpipe_link_builtin_tone_mertens_local();
    cpipe_link_builtin_tone_reinhard();
    cpipe_link_builtin_lens_shading_gainmap();
    cpipe_link_builtin_lens_dng_opcode_list_3();
    cpipe_link_builtin_fusion_hdr_plus_derivative();
    cpipe_link_builtin_precision_convert();
    cpipe_link_builtin_output_heif_sdr();
}
