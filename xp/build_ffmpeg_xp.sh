#!/usr/bin/env bash
# FFmpeg static-lib recipe for the Windows XP port.
#
# The port pins FFmpeg at release/3.4 -- the last branch the v141_xp toolchain
# can build. Everything this port needs beyond a bare "it starts" build is
# decided here, so the file has to travel WITH the sources: patches/ alone does
# not reproduce the binary.
#
# Run it inside the v141_xp (14.16 target) environment via msys2 -- the workspace
# wrapper build_ffmpeg_xp.bat sets MSYS2_PATH_TYPE=inherit and hands this script
# to msys2 bash. Machine specific paths can be overridden from the environment:
#   XP_LIBS_DIR   (msys path) third-party sources, must contain ffmpeg/ and opus/
#   XP_OPUS_LIB   (msys path) directory of the opus.lib that Telegram itself links
#
# WHAT IS ENABLED AND WHY -- do not trim without reading this:
#   * --enable-libopus + encoder=libopus  -> VOICE MESSAGE RECORDING. The build
#     used to contain ZERO encoders, so the opus muxer's audio_codec had none and
#     media_audio_capture.cpp failed with "Unable to avcodec_find_encoder".
#     NB: libopus accepts only S16/FLT -- FLTP belongs to the NATIVE opus encoder,
#     which is why media_audio_capture.cpp asks the encoder for its format.
#   * --enable-filter=atempo (+aformat/aresample/anull) -> PLAYBACK SPEED.
#     Media::Audio::SupportsSpeedControl() needs abuffer/abuffersink/atempo; the
#     two buffer filters are registered unconditionally, atempo was not built at
#     all. Filters also need avfilter_register_all() at runtime -- see
#     FFmpeg::EnsureRegistered() in ffmpeg_utility.cpp.
#   * encoder=aac + muxer=mp4/mov -> the muxing half of round video messages.
#     libopenh264 was tried and dropped again: round video ALSO needs camera
#     capture, which lives in the WebRTC-backed tgcalls sources this build
#     excludes (Calls::Instance::getVideoCapture() returns null), so the encoder
#     alone is dead weight. Re-enable it only together with a capture backend --
#     and re-run xp/xpsafe.ps1 afterwards: the prebuilt openh264 comes from a
#     modern MSVC, whose CRT objects can import Vista+ entry points.
#   * a broad set of legacy demuxers/decoders (avi, asf/wmv, flv, mpeg-ps/ts, rm,
#     ac3/dts/alac/ape/amr/adpcm, msmpeg4v1/wmv1-3/vc1, mjpeg, indeo, cinepak,
#     svq, rv) so the files people actually send to an XP machine open.
set -e

# msys2 tools (make, nasm, pkg-config, diff) first, then the inherited Windows
# PATH carrying cl/link/lib from the v141_xp environment.
export PATH="/usr/bin:/mingw64/bin:$PATH"

XP_LIBS_DIR="${XP_LIBS_DIR:-/c/TBuild/xp-port/Libraries-walk}"
XP_OPUS_LIB="${XP_OPUS_LIB:-$XP_LIBS_DIR/opus/out/Release}"
FFDIR="$XP_LIBS_DIR/ffmpeg"

# ffmpeg's configure can only find libopus through pkg-config, and the opus build
# we link ships no .pc -- so generate one instead of hand-placing a file inside a
# third-party tree. It MUST sit in <prefix>/lib/pkgconfig: msys2's pkgconf has
# --define-prefix on by default and rewrites ${prefix} from the file's own
# location (two levels up), so a flat pkgconfig/ directory silently resolves to
# the wrong tree and configure then finds nothing.
opus_prefix="$XP_LIBS_DIR/opus"
opus_pcdir="$opus_prefix/lib/pkgconfig"
mkdir -p "$opus_pcdir"
{
  echo "prefix=$(cd "$opus_prefix" && pwd -W)"
  echo 'includedir=${prefix}/include'
  echo "libdir=$(cd "$XP_OPUS_LIB" && pwd -W)"
  echo ""
  echo "Name: opus"
  echo "Description: Opus IETF audio codec (the very opus.lib Telegram links)"
  echo "Version: 1.4"
  echo 'Libs: -L${libdir} -lopus'
  echo 'Cflags: -I${includedir}'
} > "$opus_pcdir/opus.pc"
export PKG_CONFIG_PATH="$opus_pcdir"

cd "$FFDIR"

# Configure flags mirror Telegram/Patches/build_ffmpeg_win.sh plus the additions
# described above. -MT = static CRT.
./configure --toolchain=msvc \
  --extra-cflags="-MT" \
  --disable-programs --disable-doc --disable-network --disable-everything \
  --enable-libopus \
  --enable-hwaccel=h264_d3d11va --enable-hwaccel=h264_d3d11va2 --enable-hwaccel=h264_dxva2 \
  --enable-hwaccel=hevc_d3d11va --enable-hwaccel=hevc_d3d11va2 --enable-hwaccel=hevc_dxva2 \
  --enable-hwaccel=mpeg2_d3d11va --enable-hwaccel=mpeg2_d3d11va2 --enable-hwaccel=mpeg2_dxva2 \
  --enable-protocol=file \
  --enable-filter=atempo --enable-filter=aformat --enable-filter=aresample --enable-filter=anull \
  --enable-encoder=libopus --enable-encoder=aac \
  --enable-encoder=pcm_s16le \
  --enable-bsf=aac_adtstoasc --enable-bsf=h264_mp4toannexb \
  --enable-bsf=extract_extradata --enable-bsf=mpeg4_unpack_bframes \
  --enable-decoder=aac --enable-decoder=aac_fixed --enable-decoder=aac_latm \
  --enable-decoder=flac --enable-decoder=gif --enable-decoder=h264 --enable-decoder=hevc \
  --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float \
  --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float \
  --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=mpeg4 \
  --enable-decoder=msmpeg4v1 --enable-decoder=msmpeg4v2 --enable-decoder=msmpeg4v3 \
  --enable-decoder=opus \
  --enable-decoder=pcm_alaw --enable-decoder=pcm_f32be --enable-decoder=pcm_f32le \
  --enable-decoder=pcm_f64be --enable-decoder=pcm_f64le --enable-decoder=pcm_lxf \
  --enable-decoder=pcm_mulaw --enable-decoder=pcm_s16be --enable-decoder=pcm_s16be_planar \
  --enable-decoder=pcm_s16le --enable-decoder=pcm_s16le_planar --enable-decoder=pcm_s24be \
  --enable-decoder=pcm_s24daud --enable-decoder=pcm_s24le --enable-decoder=pcm_s24le_planar \
  --enable-decoder=pcm_s32be --enable-decoder=pcm_s32le --enable-decoder=pcm_s32le_planar \
  --enable-decoder=pcm_s64be --enable-decoder=pcm_s64le --enable-decoder=pcm_s8 \
  --enable-decoder=pcm_s8_planar --enable-decoder=pcm_u16be --enable-decoder=pcm_u16le \
  --enable-decoder=pcm_u24be --enable-decoder=pcm_u24le --enable-decoder=pcm_u32be \
  --enable-decoder=pcm_u32le --enable-decoder=pcm_u8 --enable-decoder=pcm_zork \
  --enable-decoder=vorbis --enable-decoder=vp8 --enable-decoder=vp9 --enable-decoder=wavpack --enable-decoder=wmalossless \
  --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice \
  --enable-decoder=mpeg1video --enable-decoder=mpeg2video \
  --enable-decoder=h263 --enable-decoder=h263i --enable-decoder=flv \
  --enable-decoder=wmv1 --enable-decoder=wmv2 --enable-decoder=wmv3 --enable-decoder=vc1 \
  --enable-decoder=vp6 --enable-decoder=vp6a --enable-decoder=vp6f \
  --enable-decoder=mjpeg --enable-decoder=mjpegb --enable-decoder=png \
  --enable-decoder=theora --enable-decoder=svq1 --enable-decoder=svq3 \
  --enable-decoder=cinepak --enable-decoder=msvideo1 --enable-decoder=msrle \
  --enable-decoder=rawvideo --enable-decoder=dvvideo \
  --enable-decoder=indeo3 --enable-decoder=indeo4 --enable-decoder=indeo5 \
  --enable-decoder=rv10 --enable-decoder=rv20 --enable-decoder=rv30 --enable-decoder=rv40 \
  --enable-decoder=ac3 --enable-decoder=eac3 --enable-decoder=dca \
  --enable-decoder=alac --enable-decoder=ape --enable-decoder=tta \
  --enable-decoder=truehd --enable-decoder=mlp \
  --enable-decoder=cook --enable-decoder=atrac3 --enable-decoder=atrac3p \
  --enable-decoder=nellymoser --enable-decoder=qdm2 --enable-decoder=sipr \
  --enable-decoder=ra_144 --enable-decoder=ra_288 \
  --enable-decoder=amrnb --enable-decoder=amrwb \
  --enable-decoder=gsm --enable-decoder=gsm_ms \
  --enable-decoder=adpcm_ima_wav --enable-decoder=adpcm_ima_qt \
  --enable-decoder=adpcm_ms --enable-decoder=adpcm_swf --enable-decoder=adpcm_yamaha \
  --enable-parser=aac --enable-parser=aac_latm --enable-parser=flac --enable-parser=h264 \
  --enable-parser=hevc --enable-parser=mpeg4video --enable-parser=mpegaudio \
  --enable-parser=opus --enable-parser=vorbis \
  --enable-parser=ac3 --enable-parser=dca --enable-parser=mpegvideo --enable-parser=vc1 \
  --enable-parser=mjpeg --enable-parser=h263 --enable-parser=vp3 --enable-parser=vp8 \
  --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=gif --enable-demuxer=h264 \
  --enable-demuxer=hevc --enable-demuxer=matroska --enable-demuxer=m4v --enable-demuxer=mov --enable-demuxer=mp3 \
  --enable-demuxer=ogg --enable-demuxer=wav \
  --enable-demuxer=avi --enable-demuxer=asf --enable-demuxer=flv --enable-demuxer=live_flv \
  --enable-demuxer=mpegts --enable-demuxer=mpegtsraw --enable-demuxer=mpegps --enable-demuxer=mpegvideo \
  --enable-demuxer=rm --enable-demuxer=ac3 --enable-demuxer=eac3 --enable-demuxer=dts \
  --enable-demuxer=ape --enable-demuxer=tta --enable-demuxer=wv --enable-demuxer=amr \
  --enable-demuxer=aiff --enable-demuxer=au --enable-demuxer=caf --enable-demuxer=w64 \
  --enable-demuxer=mpc --enable-demuxer=mpc8 \
  --enable-muxer=ogg --enable-muxer=opus --enable-muxer=mp4 --enable-muxer=mov --enable-muxer=wav

# MANDATORY after a configure change: with the msvc toolchain ffmpeg's header
# dependency tracking does not notice that config.h changed, and allcodecs.o /
# allformats.o keep the OLD component list. The libs then contain the new
# encoders and decoders while avcodec_register_all() registers none of them --
# a build that looks correct in config.h and does nothing at runtime (verified
# with ffopus_xp.exe: 0 encoders, 60 decoders, no ac3).
make clean
make -j8
echo "=== ffmpeg .a libs ==="
ls -la libavcodec/libavcodec.a libavformat/libavformat.a libavutil/libavutil.a libswscale/libswscale.a libswresample/libswresample.a libavfilter/libavfilter.a
