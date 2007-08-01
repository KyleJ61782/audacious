/*
 * This code is derivative of guess.c of Gauche-0.8.3.
 * The following is the original copyright notice.
 */

/*
 * guess.c - guessing character encoding 
 *
 *   Copyright (c) 2000-2003 Shiro Kawai, All rights reserved.
 * 
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 * 
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the authors nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "libguess.h"
#define NULL ((void *)0)

/* take precedence if scores are same. you can customize the order as: */
/* ORDER_** &highest, &second, ... &lowest */
#define ORDER_JP &utf8, &sjis, &eucj
#define ORDER_TW &utf8, &big5
#define ORDER_CN &utf8, &gb2312, &gb18030
#define ORDER_KR &utf8, &euck, &johab

/* workaround for that glib's g_convert can't convert
   properly from UCS-2BE/LE trailing after BOM. */
#define WITH_G_CONVERT 1
/* #undef WITH_G_CONVERT */

#ifdef WITH_G_CONVERT
const char UCS_2BE[] = "UTF-16";
const char UCS_2LE[] = "UTF-16";
#else
const char UCS_2BE[] = "UCS-2BE";
const char UCS_2LE[] = "UCS-2LE";
#endif

/* data types */
typedef struct guess_arc_rec
{
    unsigned int next;          /* next state */
    double score;               /* score */
} guess_arc;

typedef struct guess_dfa_rec
{
    signed char (*states)[256];
    guess_arc *arcs;
    int state;
    double score;
} guess_dfa;

/* macros */
#define DFA_INIT(st, ar) \
    { st, ar, 0, 1.0 }

#define DFA_NEXT(dfa, ch)                               \
    do {                                                \
        int arc__;                                      \
        if (dfa.state >= 0) {                           \
            arc__ = dfa.states[dfa.state][ch];          \
            if (arc__ < 0) {                            \
                dfa.state = -1;                         \
            } else {                                    \
                dfa.state = dfa.arcs[arc__].next;       \
                dfa.score *= dfa.arcs[arc__].score;     \
            }                                           \
        }                                               \
    } while (0)

#define DFA_ALIVE(dfa)  (dfa.state >= 0)

/* include DFA table generated by guess.scm */
#include "guess_tab.c"


int dfa_validate_utf8(const char *buf, int buflen)
{
    int i;
    guess_dfa utf8 = DFA_INIT(guess_utf8_st, guess_utf8_ar);

    for (i = 0; i < buflen; i++) {
        int c = (unsigned char) buf[i];

        if (DFA_ALIVE(utf8))
            DFA_NEXT(utf8, c);
        else
            break;
    }

    if(DFA_ALIVE(utf8))
        return 1;
    else 
        return 0;
}

const char *guess_jp(const char *buf, int buflen)
{
    int i;
    guess_dfa eucj = DFA_INIT(guess_eucj_st, guess_eucj_ar);
    guess_dfa sjis = DFA_INIT(guess_sjis_st, guess_sjis_ar);
    guess_dfa utf8 = DFA_INIT(guess_utf8_st, guess_utf8_ar);
    guess_dfa *top = NULL;

    guess_dfa *order[] = { ORDER_JP, NULL };

    for (i = 0; i < buflen; i++) {
        int c = (unsigned char) buf[i];

        /* special treatment of iso-2022 escape sequence */
        if (c == 0x1b) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[++i];
                if (c == '$' || c == '(')
                    return "ISO-2022-JP";
            }
        }

        /* special treatment of BOM */
        if (i == 0 && c == 0xff) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[i + 1];
                if (c == 0xfe)
                    return UCS_2LE;
            }
        }
        if (i == 0 && c == 0xfe) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[i + 1];
                if (c == 0xff)
                    return UCS_2BE;
            }
        }

        if (DFA_ALIVE(eucj)) {
            if (!DFA_ALIVE(sjis) && !DFA_ALIVE(utf8))
                return "EUC-JP";
            DFA_NEXT(eucj, c);
        }
        if (DFA_ALIVE(sjis)) {
            if (!DFA_ALIVE(eucj) && !DFA_ALIVE(utf8))
                return "SJIS";
            DFA_NEXT(sjis, c);
        }
        if (DFA_ALIVE(utf8)) {
            if (!DFA_ALIVE(sjis) && !DFA_ALIVE(eucj))
                return "UTF-8";
            DFA_NEXT(utf8, c);
        }

        if (!DFA_ALIVE(eucj) && !DFA_ALIVE(sjis) && !DFA_ALIVE(utf8)) {
            /* we ran out the possibilities */
            return NULL;
        }
    }

    /* Now, we have ambigous code.  Pick the highest score.  If more than
       one candidate tie, pick the default encoding. */
    for (i = 0; order[i] != NULL; i++) {
        if (order[i]->state >= 0) { //DFA_ALIVE()
            if (top == NULL || order[i]->score > top->score)
                top = order[i];
        }
    }

    if (top == &eucj)
        return "EUC-JP";
    if (top == &utf8)
        return "UTF-8";
    if (top == &sjis)
        return "SJIS";
    return NULL;
}

const char *guess_tw(const char *buf, int buflen)
{
    int i;
    guess_dfa big5 = DFA_INIT(guess_big5_st, guess_big5_ar);
    guess_dfa utf8 = DFA_INIT(guess_utf8_st, guess_utf8_ar);
    guess_dfa *top = NULL;

    guess_dfa *order[] = { ORDER_TW, NULL };

    for (i = 0; i < buflen; i++) {
        int c = (unsigned char) buf[i];

        /* special treatment of iso-2022 escape sequence */
        if (c == 0x1b) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[++i];
                if (c == '$' || c == '(')
                    return "ISO-2022-TW";
            }
        }

        /* special treatment of BOM */
        if (i == 0 && c == 0xff) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[i + 1];
                if (c == 0xfe)
                    return UCS_2LE;
            }
        }
        if (i == 0 && c == 0xfe) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[i + 1];
                if (c == 0xff)
                    return UCS_2BE;
            }
        }

        if (DFA_ALIVE(big5)) {
            if (!DFA_ALIVE(utf8))
                return "BIG5";
            DFA_NEXT(big5, c);
        }
        if (DFA_ALIVE(utf8)) {
            if (!DFA_ALIVE(big5))
                return "UTF-8";
            DFA_NEXT(utf8, c);
        }

        if (!DFA_ALIVE(big5) && !DFA_ALIVE(utf8)) {
            /* we ran out the possibilities */
            return NULL;
        }
    }

    /* Now, we have ambigous code.  Pick the highest score.  If more than
       one candidate tie, pick the default encoding. */
    for (i = 0; order[i] != NULL; i++) {
        if (order[i]->state >= 0) { //DFA_ALIVE()
            if (top == NULL || order[i]->score > top->score)
                top = order[i];
        }
    }

    if (top == &big5)
        return "BIG5";
    if (top == &utf8)
        return "UTF-8";
    return NULL;
}

const char *guess_cn(const char *buf, int buflen)
{
    int i;
    guess_dfa gb2312 = DFA_INIT(guess_gb2312_st, guess_gb2312_ar);
    guess_dfa utf8 = DFA_INIT(guess_utf8_st, guess_utf8_ar);
    guess_dfa gb18030 = DFA_INIT(guess_gb18030_st, guess_gb18030_ar);
    guess_dfa *top = NULL;

    guess_dfa *order[] = { ORDER_CN, NULL };

    for (i = 0; i < buflen; i++) {
        int c = (unsigned char) buf[i];
        int c2;

        /* special treatment of iso-2022 escape sequence */
        if (c == 0x1b) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[i + 1];
                c2 = (unsigned char) buf[i + 2];
                if (c == '$' && (c2 == ')' || c2 == '+'))
                    return "ISO-2022-CN";
            }
        }

        /* special treatment of BOM */
        if (i == 0 && c == 0xff) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[i + 1];
                if (c == 0xfe)
                    return UCS_2LE;
            }
        }
        if (i == 0 && c == 0xfe) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[i + 1];
                if (c == 0xff)
                    return UCS_2BE;
            }
        }

        if (DFA_ALIVE(gb2312)) {
            if (!DFA_ALIVE(utf8) && !DFA_ALIVE(gb18030))
                return "GB2312";
            DFA_NEXT(gb2312, c);
        }
        if (DFA_ALIVE(utf8)) {
            if (!DFA_ALIVE(gb2312) && !DFA_ALIVE(gb18030))
                return "UTF-8";
            DFA_NEXT(utf8, c);
        }
        if (DFA_ALIVE(gb18030)) {
            if (!DFA_ALIVE(utf8) && !DFA_ALIVE(gb2312))
                return "GB18030";
            DFA_NEXT(gb18030, c);
        }

        if (!DFA_ALIVE(gb2312) && !DFA_ALIVE(utf8) && !DFA_ALIVE(gb18030)) {
            /* we ran out the possibilities */
            return NULL;
        }
    }

    /* Now, we have ambigous code.  Pick the highest score.  If more than
       one candidate tie, pick the default encoding. */
    for (i = 0; order[i] != NULL; i++) {
        if (order[i]->state >= 0) { //DFA_ALIVE()
            if (top == NULL || order[i]->score > top->score)
                top = order[i];
        }
    }

    if (top == &gb2312)
        return "GB2312";
    if (top == &utf8)
        return "UTF-8";
    if (top == &gb18030)
        return "GB18030";
    return NULL;
}

const char *guess_kr(const char *buf, int buflen)
{
    int i;
    guess_dfa euck = DFA_INIT(guess_euck_st, guess_euck_ar);
    guess_dfa utf8 = DFA_INIT(guess_utf8_st, guess_utf8_ar);
    guess_dfa johab = DFA_INIT(guess_johab_st, guess_johab_ar);
    guess_dfa *top = NULL;

    guess_dfa *order[] = { ORDER_KR, NULL };

    for (i = 0; i < buflen; i++) {
        int c = (unsigned char) buf[i];
        int c2;

        /* special treatment of iso-2022 escape sequence */
        if (c == 0x1b) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[i + 1];
                c2 = (unsigned char) buf[i + 2];
                if (c == '$' && c2 == ')')
                    return "ISO-2022-KR";
            }
        }

        /* special treatment of BOM */
        if (i == 0 && c == 0xff) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[i + 1];
                if (c == 0xfe)
                    return UCS_2LE;
            }
        }
        if (i == 0 && c == 0xfe) {
            if (i < buflen - 1) {
                c = (unsigned char) buf[i + 1];
                if (c == 0xff)
                    return UCS_2BE;
            }
        }

        if (DFA_ALIVE(euck)) {
            if (!DFA_ALIVE(johab) && !DFA_ALIVE(utf8))
                return "EUC-KR";
            DFA_NEXT(euck, c);
        }
        if (DFA_ALIVE(johab)) {
            if (!DFA_ALIVE(euck) && !DFA_ALIVE(utf8))
                return "JOHAB";
            DFA_NEXT(johab, c);
        }
        if (DFA_ALIVE(utf8)) {
            if (!DFA_ALIVE(euck) && !DFA_ALIVE(johab))
                return "UTF-8";
            DFA_NEXT(utf8, c);
        }

        if (!DFA_ALIVE(euck) && !DFA_ALIVE(johab) && !DFA_ALIVE(utf8)) {
            /* we ran out the possibilities */
            return NULL;
        }
    }

    /* Now, we have ambigous code.  Pick the highest score.  If more than
       one candidate tie, pick the default encoding. */
    for (i = 0; order[i] != NULL; i++) {
        if (order[i]->state >= 0) { //DFA_ALIVE()
            if (top == NULL || order[i]->score > top->score)
                top = order[i];
        }
    }

    if (top == &euck)
        return "EUC-KR";
    if (top == &utf8)
        return "UTF-8";
    if (top == &johab)
        return "JOHAB";
    return NULL;
}

typedef struct _guess_impl {
    struct _guess_impl *next;
    const char *name;
    const char *(*impl)(const char *buf, int len);
} guess_impl;

static guess_impl *guess_impl_list = NULL;

void guess_impl_register(const char *lang,
    const char *(*impl)(const char *buf, int len))
{
    guess_impl *iptr = calloc(sizeof(guess_impl), 1);

    iptr->name = lang;
    iptr->impl = impl;
    iptr->next = guess_impl_list;

    guess_impl_list = iptr;
}

void guess_init(void)
{
    /* check if already initialized */
    if (guess_impl_list != NULL)
        return;

    guess_impl_register(GUESS_REGION_JP, guess_jp);
    guess_impl_register(GUESS_REGION_TW, guess_tw);
    guess_impl_register(GUESS_REGION_CN, guess_cn);
    guess_impl_register(GUESS_REGION_KR, guess_kr);
    guess_impl_register(GUESS_REGION_RU, guess_ru);
}

const char *guess_encoding(const char *inbuf, int buflen, const char *lang)
{
    guess_impl *iter;

    guess_init();

    for (iter = guess_impl_list; iter != NULL; iter = iter->next)
    {
        if (!strcasecmp(lang, iter->name))
           return iter->impl(inbuf, buflen);
    }

    /* TODO: try other languages as fallback? */

    return NULL;
}
