/* MIT. Exact raw screencap boundary for the pinned 480x800 sRGB guest.
 * AOSP writes width/height/format/colorspace followed by packed rows. Reject
 * rotation, other formats and malformed lengths; the viewer can fall back to PNG.
 */
#define ANDROID_RAW_FRAME_SIZE (16U + 480U * 800U * 4U)

static int android_raw_frame_valid(const unsigned char *bytes, size_t size) {
    if (size != ANDROID_RAW_FRAME_SIZE)
        return 0;
    uint32_t header[4];
    memcpy(header, bytes, sizeof(header));
    return header[0] == 480 && header[1] == 800 && header[2] == 1 && header[3] == 1;
}
