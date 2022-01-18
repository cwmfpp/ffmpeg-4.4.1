/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


#ifndef AVUTIL_HJK_CHECK_H
#define AVUTIL_HJK_CHECK_H

typedef int HJKresult;
#define HJKAPI 

typedef HJKresult HJKAPI hjk_check_GetErrorName_cb(HJKresult error, const char** pstr);
typedef HJKresult HJKAPI hjk_check_GetErrorString(HJKresult error, const char** pstr);

/**
 * Wrap a HJK function call and print error information if it fails.
 */
static inline int ff_hjk_check(void *avctx,
                                void *hjkGetErrorName_fn, void *hjkGetErrorString_fn,
                                HJKresult err, const char *func)
{
    #define HJK_SUCCESS 0
    const char *err_name;

    av_log(avctx, AV_LOG_TRACE, "Calling %s\n", func);

    const char *err_string;


    if (err == HJK_SUCCESS)
        return 0;

    av_log(avctx, AV_LOG_ERROR, "%s failed", func);
    ((hjk_check_GetErrorName_cb *)hjkGetErrorName_fn)(err, &err_name);
    av_log(avctx, AV_LOG_ERROR, " -> %s", err_name);
    av_log(avctx, AV_LOG_ERROR, "\n");
    return AVERROR_EXTERNAL;
#ifdef ABCDE
    ((hjk_check_GetErrorString *)hjkGetErrorString_fn)(err, &err_string);

    if (err_name && err_string)
        av_log(avctx, AV_LOG_ERROR, " -> %s: %s", err_name, err_string);
    av_log(avctx, AV_LOG_ERROR, "\n");

    return AVERROR_EXTERNAL;
#endif /* ABCDE */
}

/**
 * Convenience wrapper for ff_hjk_check when directly linking libhjk.
 */

#define FF_HJK_CHECK(avclass, x) ff_hjk_check(avclass, hjkGetErrorName, hjkGetErrorString, (x), #x)

/**
 * Convenience wrapper for ff_hjk_check when dynamically loading hjk symbols.
 */

#define FF_HJK_CHECK_DL(avclass, hjkdl, x) ff_hjk_check(avclass, hjkdl->hjkGetErrorName, /*hjkdl->hjkGetErrorString*/ NULL, (x), #x)

#endif /* AVUTIL_HJK_CHECK_H */
