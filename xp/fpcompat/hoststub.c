/* A single object so that fpcompat_host.lib is never an empty archive.
 *
 * cmake/options_win.cmake links ${xp_fpcompat_loc}/fpcompat_host.lib into
 * common_options unconditionally, so the file has to exist for every
 * architecture. On x86 it carries the CRT float helpers lifted out of the 14.44
 * libcmt.lib (ftol2/ftol3 and the __isa_inverted constant they refer to). The
 * x64 compiler emits no such calls - double->integer conversion is an inline
 * SSE2 instruction there - so that archive would have nothing in it, and
 * lib.exe refuses to write one with no members. This is what fills it.
 *
 * It must NOT contain the Fls or NUMA thunks: fpcompat_host.lib is linked into
 * the code generators too, and those RUN on the build machine, where hijacking
 * FLS corrupts the CRT's per-thread state.
 */
void xp_fpcompat_host_placeholder(void) {
}
