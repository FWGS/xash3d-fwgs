/*
 * Minimal module list for Xash3D FWGS, replaces freetype's own
 * include/freetype/config/ftmodule.h through FT_CONFIG_MODULES_H.
 *
 * Only TrueType (through sfnt) and Windows FNT/FON bitmap fonts are
 * supported, everything else is left out. Keep in sync with the source
 * list in wscript.
 */

FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, winfnt_driver_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )

/* EOF */
