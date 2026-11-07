/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See muselib.h. */
#include "muselib.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* What FFmpeg on the device was measured playing, plus the containers those
 * codecs arrive in. An .mp4 is the same container as an .m4a, often with
 * nothing but sound in it; a video in one plays its sound alone. */
static const char *AUDIO[] = {
	"mp3", "m4a", "m4b", "mp4", "aac", "flac", "ogg", "oga", "opus", "wav", NULL
};

bool ml_is_audio(const char *name)
{
	const char *dot = strrchr(name, '.');
	int i;

	if (!dot || name[0] == '.') return false;
	for (i = 0; AUDIO[i]; i++)
		if (!strcasecmp(dot + 1, AUDIO[i])) return true;
	return false;
}

bool ml_is_playlist(const char *name)
{
	const char *dot = strrchr(name, '.');

	if (!dot || name[0] == '.') return false;
	return !strcasecmp(dot + 1, "m3u") || !strcasecmp(dot + 1, "m3u8");
}

/* Whether `s` joins its words with dots or underscores instead of spaces:
 * no space at all, two words or more, and one of them three letters or more,
 * so "Used.To.Be" is words and "R.E.M." is initials */
static bool scene_style(const char *s)
{
	int words = 0, long_words = 0, run = 0;

	if (strchr(s, ' ')) return false;
	for (; ; s++) {
		if (*s && *s != '.' && *s != '_') { run++; continue; }
		if (run) words++;
		if (run >= 3) long_words++;
		run = 0;
		if (!*s) break;
	}
	return words >= 2 && long_words >= 1;
}

/* Spaces for dots and underscores, then one space at a time and none at the
 * ends. True when it changed anything. */
static bool unscene(char *s)
{
	char *r, *w;

	if (!scene_style(s)) return false;
	for (r = s; *r; r++)
		if (*r == '.' || *r == '_') *r = ' ';
	for (r = w = s; *r; r++)
		if (*r != ' ' || (w > s && w[-1] != ' ')) *w++ = *r;
	while (w > s && w[-1] == ' ') w--;
	*w = '\0';
	return true;
}

void ml_clean_words(char *s)
{
	char nfc[256];

	unscene(s);
	if (strlen(s) < sizeof nfc) {
		size_t len = ml_utf8_nfd_to_nfc(s, nfc, sizeof nfc);
		if (len > 0 && len < sizeof nfc) memcpy(s, nfc, len + 1);
	}
}

/* A word that only says what format or bitrate a download was */
static bool format_word(const char *w, size_t n)
{
	static const char *const WORDS[] = {
		"mp3", "flac", "aac", "m4a", "ogg", "opus", "wav", "web", "webrip",
		"cd", "cdrip", "vinyl", "kbps", "320kbps", "256kbps", "192kbps",
		"128kbps", "v0", "v2", "16bit", "24bit", "44khz", "48khz", NULL
	};
	int i;

	for (i = 0; WORDS[i]; i++)
		if (strlen(WORDS[i]) == n && !strncasecmp(w, WORDS[i], n)) return true;
	return false;
}

void ml_clean_album(char *s, const char *artist)
{
	char out[128], clean_artist[128];
	const char *p, *word;
	size_t o = 0, al;
	bool first = true;

	if (!unscene(s)) return;
	/* Its artist in front, when that leaves a name behind */
	snprintf(clean_artist, sizeof clean_artist, "%s", artist ? artist : "");
	unscene(clean_artist);
	al = strlen(clean_artist);
	p = s;
	if (al && !strncasecmp(p, clean_artist, al) && !strncmp(p + al, " - ", 3) && p[al + 3])
		p += al + 3;
	/* Word by word, up to a release year, without the format words */
	while (*p) {
		size_t n;

		while (*p == ' ') p++;
		if (!*p) break;
		word = p;
		while (*p && *p != ' ') p++;
		n = (size_t)(p - word);
		if (!first && n == 4 && (word[0] == '1' || word[0] == '2') &&
		    isdigit((unsigned char)word[1]) && isdigit((unsigned char)word[2]) &&
		    isdigit((unsigned char)word[3]) &&
		    (!strncmp(word, "19", 2) || !strncmp(word, "20", 2)))
			break;
		first = false;
		if (format_word(word, n)) continue;
		if (o && o + 1 < sizeof out) out[o++] = ' ';
		if (o + n >= sizeof out) n = sizeof out - o - 1;
		memcpy(out + o, word, n);
		o += n;
	}
	out[o] = '\0';
	/* A trailing lone dash, left where "Artist - " was the whole of it */
	while (o > 0 && (out[o - 1] == '-' || out[o - 1] == ' ')) out[--o] = '\0';
	if (o) snprintf(s, 128, "%s", out);
}

/* A name rather than a number: "1.5.mp3" is not track 1 of something */
static bool has_letter(const char *s)
{
	for (; *s; s++)
		if (isalpha((unsigned char)*s)) return true;
	return false;
}

bool ml_tidy_file(const char *name, char *out, size_t n)
{
	const char *dot = strrchr(name, '.'), *p = name, *q;
	char stem[256], num[8] = "";
	size_t len;
	bool dotted = false;    /* the number joined on by dots or underscores alone */

	if (!dot || dot == name) return false;
	/* The track number, up to three digits, and whatever joined it on */
	while (isdigit((unsigned char)*p)) p++;
	q = p;
	while (*q == ' ' || *q == '-' || *q == '.' || *q == '_') q++;
	if (p > name && p - name <= 3 && q > p && q < dot) {
		snprintf(num, sizeof num, "%.*s ", (int)(p - name), name);
		dotted = strspn(p, "._") == (size_t)(q - p);
		p = q;
	} else {
		p = name;
	}
	len = (size_t)(dot - p);
	if (len >= sizeof stem) return false;
	memcpy(stem, p, len);
	stem[len] = '\0';
	/* A title of one word has nothing to unscene, but "03..Toes" is the same
	 * download as "01..American.Music" and loses its dots the same way */
	if (!unscene(stem) && !(dotted && has_letter(stem))) return false;
	if (!stem[0]) return false;
	if ((size_t)snprintf(out, n, "%s%s%s", num, stem, dot) >= n) return false;
	return strcmp(out, name) != 0;
}

bool ml_tidy_folder(const char *name, const char *artist, char *out, size_t n)
{
	char s[128];

	if (strlen(name) >= sizeof s) return false;
	snprintf(s, sizeof s, "%s", name);
	ml_clean_album(s, artist);
	if (!s[0] || !strcmp(s, name)) return false;
	return (size_t)snprintf(out, n, "%s", s) < n;
}

void ml_track_name(const char *file, char *out, int n)
{
	const char *p = file, *dot = strrchr(file, '.');
	int len;

	/* "03 ", "03 - ", "03. ", "3-" - a number and its punctuation, but only
	 * when a name follows: a track CALLED "1979" keeps its name. */
	while (isdigit((unsigned char)*p)) p++;
	if (p > file && p - file <= 3) {
		const char *q = p;

		while (*q == ' ' || *q == '-' || *q == '.' || *q == '_') q++;
		if (q > p && *q && q != dot) p = q; else p = file;
	} else {
		p = file;
	}
	len = dot && dot > p ? (int)(dot - p) : (int)strlen(p);
	if (len >= n) len = n - 1;
	memcpy(out, p, (size_t)len);
	out[len] = '\0';
	ml_clean_words(out);
}

/* UTF-8 NFD → NFC cho tên file trên macOS (HFS+ lưu tên dưới dạng tổ hợp,
 * ví dụ "á" = "a" + U+0300). Dấu được viết lại thành mẫu đơn khi có thể, và
 * các ký tự khác giữ nguyên. Không phải bộ mã hóa Unicode đầy đủ - chỉ các
 * dấu kết hợp phổ biến trong tiếng Việt, đủ cho hiển thị trên menu. */
static unsigned utf8_decode(const char **p, const char *end)
{
	const unsigned char *s = (const unsigned char *)*p;
	unsigned c;

	if (s >= (const unsigned char *)end) return 0;
	if (*s < 0x80) { (*p)++; return *s; }
	if ((*s & 0xE0) == 0xC0 && s + 1 < (const unsigned char *)end) {
		c = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
		*p += 2; return c;
	}
	if ((*s & 0xF0) == 0xE0 && s + 2 < (const unsigned char *)end) {
		c = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
		*p += 3; return c;
	}
	if ((*s & 0xF8) == 0xF0 && s + 3 < (const unsigned char *)end) {
		c = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
		    ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
		*p += 4; return c;
	}
	(*p)++; return '?';
}

/* Ghi mã điểm Unicode `c` dưới dạng UTF-8 vào `*p`, dùng hết `end`. */
static int utf8_encode(unsigned c, char **p, const char *end)
{
	char *w = *p;

	if (c < 0x80) {
		if (w + 1 >= end) return 0;
		*w++ = (char)c;
	} else if (c < 0x800) {
		if (w + 2 >= end) return 0;
		*w++ = (char)(0xC0 | (c >> 6));
		*w++ = (char)(0x80 | (c & 0x3F));
	} else if (c < 0x10000) {
		if (w + 3 >= end) return 0;
		*w++ = (char)(0xE0 | (c >> 12));
		*w++ = (char)(0x80 | ((c >> 6) & 0x3F));
		*w++ = (char)(0x80 | (c & 0x3F));
	} else {
		if (w + 4 >= end) return 0;
		*w++ = (char)(0xF0 | (c >> 18));
		*w++ = (char)(0x80 | ((c >> 12) & 0x3F));
		*w++ = (char)(0x80 | ((c >> 6) & 0x3F));
		*w++ = (char)(0x80 | (c & 0x3F));
	}
	*p = w;
	return 1;
}

/* Một cặp (chữ cái chính, dấu kết hợp) → chữ cái đơn có dấu. Trả 0 khi không
 * phải cặp này. Dấu tiếng Việt: grave U+0300, acute U+0301, hook above U+0309,
 * dot below U+0323, tilde U+0303. */
static unsigned compose_pair(unsigned base, unsigned mark)
{
	/* base + combining grave (U+0300) */
	if (mark == 0x0300) {
		switch (base) {
		case 'a': return 0x00E0; case 'A': return 0x00C0;
		case 'e': return 0x00E8; case 'E': return 0x00C8;
		case 'i': return 0x00EC; case 'I': return 0x00CC;
		case 'o': return 0x00F2; case 'O': return 0x00D2;
		case 'u': return 0x00F9; case 'U': return 0x00D9;
		case 'y': return 0x1EF3; case 'Y': return 0x1EF2;
		}
	}
	/* base + combining acute (U+0301) */
	if (mark == 0x0301) {
		switch (base) {
		case 'a': return 0x00E1; case 'A': return 0x00C1;
		case 'e': return 0x00E9; case 'E': return 0x00C9;
		case 'i': return 0x00ED; case 'I': return 0x00CD;
		case 'o': return 0x00F3; case 'O': return 0x00D3;
		case 'u': return 0x00FA; case 'U': return 0x00DA;
		case 'y': return 0x1EF5; case 'Y': return 0x1EF4;
		}
	}
	/* base + combining hook above (U+0309) */
	if (mark == 0x0309) {
		switch (base) {
		case 'a': return 0x1EA3; case 'A': return 0x1EA2;
		case 'e': return 0x1EB9; case 'E': return 0x1EB8;
		case 'i': return 0x1ECB; case 'I': return 0x1ECA;
		case 'o': return 0x1ECD; case 'O': return 0x1ECC;
		case 'u': return 0x1EE9; case 'U': return 0x1EE8;
		case 'y': return 0x1EF7; case 'Y': return 0x1EF6;
		}
	}
	/* base + combining dot below (U+0323) */
	if (mark == 0x0323) {
		switch (base) {
		case 'a': return 0x1EA1; case 'A': return 0x1EA0;
		case 'd': return 0x1E0F; case 'D': return 0x1E0E;
		case 'e': return 0x1EB1; case 'E': return 0x1EB0;
		case 'o': return 0x1ECE; case 'O': return 0x1ECD;
		case 'u': return 0x1EE7; case 'U': return 0x1EE6;
		case 'y': return 0x1EF6; case 'Y': return 0x1EF5;
		}
	}
	/* base + combining tilde (U+0303) */
	if (mark == 0x0303) {
		switch (base) {
		case 'a': return 0x00E3; case 'A': return 0x00C3;
		case 'e': return 0x1EBD; case 'E': return 0x1EBC;
		case 'i': return 0x0129; case 'I': return 0x0128;
		case 'o': return 0x00F5; case 'O': return 0x00D5;
		case 'u': return 0x0169; case 'U': return 0x0168;
		case 'y': return 0x1EF9; case 'Y': return 0x1EF8;
		}
	}
	return 0;
}

/* `in` dưới dạng NFD (chữ cái + dấu kết hợp riêng) về NFC (chữ cái đơn có
 * dấu) trong `out`. `outn` là kích thước `out`. Trả độ dài của `out`, không
 * tính NUL. Dấu tiếng Việt được ghép lại; các ký tự khác giữ nguyên. */
size_t ml_utf8_nfd_to_nfc(const char *in, char *out, size_t outn)
{
	const char *p = in, *end = in + strlen(in);
	char *w = out;
	unsigned c, mark = 0;
	bool have_mark = false;

	while (p < end && w + 1 < out + outn) {
		c = utf8_decode(&p, end);
		/* Dấu kết hợp U+0300-U+036F. */
		if (c >= 0x0300 && c <= 0x036F && !have_mark) {
			mark = c;
			have_mark = true;
			continue;
		}
		if (have_mark) {
			unsigned cp = compose_pair(c, mark);
			if (cp) {
				if (!utf8_encode(cp, &w, out + outn)) break;
			} else {
				if (!utf8_encode(mark, &w, out + outn)) break;
				if (!utf8_encode(c, &w, out + outn)) break;
			}
			have_mark = false;
			mark = 0;
		} else {
			if (!utf8_encode(c, &w, out + outn)) break;
		}
	}
	if (have_mark && w + 1 < out + outn) {
		if (!utf8_encode(mark, &w, out + outn)) {}
	}
	*w = '\0';
	return (size_t)(w - out);
}
static size_t dir_len(const char *path)
{
	const char *s = strrchr(path, '/');

	return s ? (size_t)(s - path) : 0;
}

int ml_album_of(const ml_lib *l, const char *track)
{
	size_t n;
	int i;

	if (!track || !track[0]) return -1;
	n = dir_len(track);
	for (i = 0; i < l->nalbums; i++) {
		const char *p;

		if (l->albums[i].n <= 0) continue;
		p = l->tracks[l->albums[i].first].path;
		/* The same length first: "Pixies/Doolittle" is a prefix of
		 * "Pixies/Doolittle (Deluxe)", and they are two albums. */
		if (dir_len(p) == n && !strncmp(p, track, n)) return i;
	}
	return -1;
}

void ml_cover_base(const char *root, const ml_lib *l, int al, char *out, size_t n)
{
	const char *p;
	size_t d, cut;

	out[0] = '\0';
	if (al < 0 || al >= l->nalbums || l->albums[al].n <= 0) return;
	p = l->tracks[l->albums[al].first].path;
	d = dir_len(p);                         /* "Radiohead/The Bends" */
	for (cut = d; cut > 0 && p[cut - 1] != '/'; cut--) { }
	/* p[0..cut) is the parent with its slash, p[cut..d) the album's name. */
	snprintf(out, n, "%s/%.*s.media/%.*s", root, (int)cut, p,
	         (int)(d - cut), p + cut);
}

/* ---- a small growable list of names, sorted ------------------------------- */

typedef struct { char **v; int n, cap; } names;

static void names_add(names *l, const char *s)
{
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 32;
		char **v = realloc(l->v, sizeof *v * (size_t)cap);

		if (!v) return;
		l->v = v;
		l->cap = cap;
	}
	l->v[l->n] = strdup(s);
	if (l->v[l->n]) l->n++;
}

static int cmp_name(const void *a, const void *b)
{
	return strcasecmp(*(char *const *)a, *(char *const *)b);
}

static void names_free(names *l)
{
	int i;

	for (i = 0; i < l->n; i++) free(l->v[i]);
	free(l->v);
	memset(l, 0, sizeof *l);
}

/* The subfolders and the audio files in `dir`, each sorted. Dot entries are
 * skipped: macOS leaves AppleDouble files beside every copied track, and they
 * carry the track's own extension. */
static void list_dir(const char *dir, names *dirs, names *files,
                     names *pls)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d) return;
	while ((e = readdir(d))) {
		char full[LIB_PATH * 2];
		struct stat st;

		if (e->d_name[0] == '.') continue;
		if (snprintf(full, sizeof full, "%s/%s", dir, e->d_name) >= (int)sizeof full)
			continue;
		if (stat(full, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) { if (dirs) names_add(dirs, e->d_name); }
		else {
			if (files && ml_is_audio(e->d_name)) names_add(files, e->d_name);
			if (pls && ml_is_playlist(e->d_name)) names_add(pls, e->d_name);
		}
	}
	closedir(d);
	if (dirs)  qsort(dirs->v, (size_t)dirs->n, sizeof *dirs->v, cmp_name);
	if (files) qsort(files->v, (size_t)files->n, sizeof *files->v, cmp_name);
	if (pls)  qsort(pls->v, (size_t)pls->n, sizeof *pls->v, cmp_name);
}

/* ---- the library ---------------------------------------------------------- */

static bool grow(void **v, int n, int *cap, size_t sz)
{
	void *p;
	int c;

	if (n < *cap) return true;
	c = *cap ? *cap * 2 : 64;
	p = realloc(*v, sz * (size_t)c);
	if (!p) return false;
	*v = p;
	*cap = c;
	return true;
}

/* One album: `rel` is its folder relative to the root. */
static void add_album(ml_lib *l, int *acap, int *tcap, bool book,
                      const char *rel, const char *name, const names *files)
{
	ml_album *al;
	int i;

	if (files->n == 0) return;
	if (!grow((void **)&l->albums, l->nalbums, acap, sizeof *l->albums)) return;
	al = &l->albums[l->nalbums++];
	snprintf(al->name, sizeof al->name, "%s", name);
	al->book = book;
	al->singles = false;
	al->first = l->ntracks;
	al->n = 0;
	for (i = 0; i < files->n; i++) {
		ml_track *t;
		int len;

		if (!grow((void **)&l->tracks, l->ntracks, tcap, sizeof *l->tracks)) break;
		t = &l->tracks[l->ntracks];
		if (rel[0])
			len = snprintf(t->path, sizeof t->path, "%s/%s", rel, files->v[i]);
		else
			len = snprintf(t->path, sizeof t->path, "%s", files->v[i]);
		if (len >= (int)sizeof t->path) continue;
		ml_track_name(files->v[i], t->name, sizeof t->name);
		l->ntracks++;
		al->n++;
	}
}

/* Read a .m3u/.m3u8 playlist. Each non-blank, non-comment line is a track
 * path, relative to `music_root`. Paths that climb out of `music_root`
 * ("..", leading "/", absolute) are refused - the same rule xfer_resolve
 * uses for every path the browser sends. Returns the number of tracks read,
 * or -1 on error. `out` must hold at least `max` entries. */
int ml_playlist_read(const char *playlist_path, const char *music_root,
                     ml_track *out, int max)
{
	FILE *f;
	char line[512];
	int n = 0;

	if (!playlist_path || !music_root || !out || max <= 0) return -1;
	f = fopen(playlist_path, "r");
	if (!f) return -1;
	while (fgets(line, sizeof line, f)) {
		char *p = line, *end;
		char abs[LIB_PATH * 2];
		size_t rootlen;

		/* Strip trailing newline and whitespace. */
		end = line + strlen(line);
		while (end > line && (end[-1] == '\n' || end[-1] == '\r' ||
		                      end[-1] == ' ' || end[-1] == '\t'))
			*--end = '\0';
		while (*p == ' ' || *p == '\t') p++;
		if (!*p || *p == '#') continue;      /* blank or comment */

		/* Absolute paths are refused: a playlist cannot reach outside
		 * Music/ by naming /mnt/SDCARD/... directly. */
		if (*p == '/') continue;

		/* Component by component: ".." anywhere means escape. */
		{
			const char *seg = p;
			bool bad = false;
			while (*seg) {
				const char *next = strchr(seg, '/');
				size_t seglen = next ? (size_t)(next - seg) : strlen(seg);
				if (seglen == 2 && seg[0] == '.' && seg[1] == '.') { bad = true; break; }
				if (seglen == 1 && seg[0] == '.') { /* skip "." */ }
				if (next) seg = next + 1; else break;
			}
			if (bad) continue;
		}

		rootlen = strlen(music_root);
		if (rootlen + 1 + strlen(p) >= sizeof abs) continue;
		snprintf(abs, sizeof abs, "%s/%s", music_root, p);

		/* Only audio files are playable from a playlist. */
		if (!ml_is_audio(abs)) continue;

		if (n >= max) break;
		snprintf(out[n].path, sizeof out[n].path, "%s", p);
		ml_track_name(p, out[n].name, sizeof out[n].name);
		n++;
	}
	fclose(f);
	return n;
}

/* Parse "[mm:ss.xx]text" or "[mm:ss,xx]txt" into seconds + text. Returns
 * seconds, or -1 when the line is not a timestamped lyric line. */
static double lrc_time(const char *s, const char **text)
{
	int m, s_;
	double ms = 0;
	char sep;

	/* [mm:ss.xx] or [mm:ss,xx] */
	if (sscanf(s, "[%d:%d%c%lf]", &m, &s_, &sep, &ms) != 4) return -1;
	if (sep != '.' && sep != ',') return -1;
	if (m < 0 || s_ < 0 || s_ >= 60) return -1;
	*text = strchr(s, ']');
	if (!*text) return -1;
	(*text)++;
	return m * 60 + s_ + ms / 100.0;
}

int ml_lrc_read(const char *audio_path, ml_lrc_line *out)
{
	char lrc[LIB_PATH], line[512];
	FILE *f;
	int n = 0;

	if (!audio_path || !out) return -1;
	snprintf(lrc, sizeof lrc, "%s", audio_path);
	{
		char *dot = strrchr(lrc, '.');
		if (dot) snprintf(dot, sizeof lrc - (size_t)(dot - lrc), ".lrc");
	}
	f = fopen(lrc, "r");
	if (!f) return -1;
	while (fgets(line, sizeof line, f)) {
		const char *text;
		double at;

		at = lrc_time(line, &text);
		if (at < 0 || !text) continue;
		if (n >= ML_LRC_MAX) break;
		out[n].at = at;
		snprintf(out[n].line, sizeof out[n].line, "%s", text);
		n++;
	}
	fclose(f);
	return n;
}

int ml_lrc_at(const ml_lrc_line *lrc, int n, double at, int *next)
{
	int cur = -1, nx = -1, i;

	if (!lrc || n <= 0) { if (next) *next = -1; return -1; }
	for (i = 0; i < n; i++) {
		if (lrc[i].at <= at + 0.05) cur = i;      /* small slack */
		else { nx = i; break; }
	}
	if (next) *next = nx;
	return cur;
}

/* How far each array has grown, across the folders one library is read from. */
typedef struct { int a, t, r; } caps;

/* `root` into `out`, after whatever is there already. Paths are `prefix/` and
 * then relative to `root`, or relative to it when `prefix` is empty. */
static bool scan_into(ml_lib *out, caps *c, const char *root, const char *prefix,
                      bool book)
{
	names top = { 0 }, pls = { 0 };
	int i;
	struct stat st;

	if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) return false;

	list_dir(root, &top, NULL, &pls);
	for (i = 0; i < top.n; i++) {
		char dir[LIB_PATH * 2];
		names sub = { 0 }, files = { 0 };
		ml_artist *ar;
		int j, before = out->nalbums;

		char top_rel[LIB_PATH];

		snprintf(dir, sizeof dir, "%s/%s", root, top.v[i]);
		list_dir(dir, &sub, &files, NULL);
		if (prefix[0]) snprintf(top_rel, sizeof top_rel, "%s/%s", prefix, top.v[i]);
		else           snprintf(top_rel, sizeof top_rel, "%s", top.v[i]);

		/* Music/<name>/<tracks>: its own artist, one album of the same name. */
		add_album(out, &c->a, &c->t, book, top_rel, top.v[i], &files);
		if (!book && out->nalbums > before && !strcmp(top.v[i], ML_SINGLES))
			out->albums[before].singles = true;
		for (j = 0; j < sub.n; j++) {
			char adir[LIB_PATH * 2], rel[LIB_PATH];
			names tracks = { 0 };

			snprintf(adir, sizeof adir, "%s/%s", dir, sub.v[j]);
			snprintf(rel, sizeof rel, "%s/%s", top_rel, sub.v[j]);
			list_dir(adir, NULL, &tracks, NULL);
			add_album(out, &c->a, &c->t, book, rel, sub.v[j], &tracks);
			names_free(&tracks);
		}
		names_free(&sub);
		names_free(&files);

		if (out->nalbums == before) continue;          /* nothing to play */
		if (!grow((void **)&out->artists, out->nartists, &c->r, sizeof *out->artists))
			break;
		ar = &out->artists[out->nartists++];
		snprintf(ar->name, sizeof ar->name, "%s", top.v[i]);
		/* The shelf's names, the folders' untouched: an album's loses its
		 * artist in front, which only the folder's own name still has */
		for (j = before; j < out->nalbums; j++)
			if (strcmp(out->albums[j].name, top.v[i]))
				ml_clean_album(out->albums[j].name, top.v[i]);
			else
				ml_clean_album(out->albums[j].name, NULL);
		ml_clean_words(ar->name);
		ar->first = before;
		ar->n = out->nalbums - before;
		ar->book = book;
	}
	/* Mỗi file .m3u/.m3u8 ở `root` là một playlist: một album có tên theo file,
	 * các bài là những đường dẫn trong nó, và chắc chắn không thoát khỏi root. */
	for (i = 0; i < pls.n; i++) {
		char pl_path[LIB_PATH * 2], pl_rel[LIB_PATH];
		ml_track pl_trk[256];
		int ntrk, before = out->nalbums, j;

		if (prefix[0]) snprintf(pl_rel, sizeof pl_rel, "%s/%s", prefix, pls.v[i]);
		else           snprintf(pl_rel, sizeof pl_rel, "%s", pls.v[i]);
		snprintf(pl_path, sizeof pl_path, "%s/%s", root, pls.v[i]);
		ntrk = ml_playlist_read(pl_path, root, pl_trk,
		                        (int)(sizeof pl_trk / sizeof pl_trk[0]));
		if (ntrk <= 0) continue;
		if (!grow((void **)&out->albums, out->nalbums, &c->a, sizeof *out->albums)) break;
		{
			ml_album *al = &out->albums[out->nalbums++];
			const char *dot = strrchr(pls.v[i], '.');
			size_t stem = dot ? (size_t)(dot - pls.v[i]) : strlen(pls.v[i]);

			if (stem >= sizeof al->name) stem = sizeof al->name - 1;
			memcpy(al->name, pls.v[i], stem);
			al->name[stem] = '\0';
			al->book = book;
			al->singles = false;
			al->first = out->ntracks;
			al->n = 0;
			for (j = 0; j < ntrk; j++) {
				ml_track *t;
				if (!grow((void **)&out->tracks, out->ntracks, &c->t, sizeof *out->tracks)) break;
				t = &out->tracks[out->ntracks];
				snprintf(t->path, sizeof t->path, "%s/%s", pl_rel, pl_trk[j].path);
				snprintf(t->name, sizeof t->name, "%s", pl_trk[j].name);
				out->ntracks++;
				al->n++;
			}
		}
		if (out->nalbums > before) {
			if (!grow((void **)&out->artists, out->nartists, &c->r, sizeof *out->artists)) break;
			ml_artist *ar = &out->artists[out->nartists++];
			snprintf(ar->name, sizeof ar->name, "%s", "Playlists");
			ar->first = before;
			ar->n = out->nalbums - before;
			ar->book = book;
		}
	}
	names_free(&top);
	names_free(&pls);
	return true;
}

bool ml_scan(const char *root, ml_lib *out)
{
	caps c = { 0 };

	memset(out, 0, sizeof *out);
	return scan_into(out, &c, root, "", false);
}

bool ml_scan_card(const char *card, ml_lib *out)
{
	char dir[LIB_PATH * 2];
	caps c = { 0 };
	bool music, books;

	memset(out, 0, sizeof *out);
	snprintf(dir, sizeof dir, "%s/Music", card);
	music = scan_into(out, &c, dir, "Music", false);
	/* File âm thanh thẳng ở gốc Music/ (không trong thư mục nghệ sĩ nào)
	 * vẫn phải có trên kệ: gom vào một album "Loose Files". */
	{
		names files = { 0 };
		list_dir(dir, NULL, &files, NULL);
		if (files.n > 0) {
			int before = out->nalbums;
			add_album(out, &c.a, &c.t, false, "Music", "Loose Files", &files);
			if (out->nalbums > before) {
				out->albums[before].singles = true;
				if (grow((void **)&out->artists, out->nartists, &c.r,
				         sizeof *out->artists)) {
					ml_artist *ar = &out->artists[out->nartists++];
					snprintf(ar->name, sizeof ar->name, "%s", "Loose Files");
					ar->first = before;
					ar->n = out->nalbums - before;
					ar->book = false;
				}
			}
		}
		names_free(&files);
	}
	snprintf(dir, sizeof dir, "%s/Audiobooks", card);
	books = scan_into(out, &c, dir, "Audiobooks", true);
	return music || books;
}

int ml_count(const ml_lib *l, bool books)
{
	int i, n = 0;

	for (i = 0; i < l->nalbums; i++)
		if (l->albums[i].book == books) n++;
	return n;
}

bool ml_folder_image(const char *root, const ml_lib *l, int al, char *out, size_t n)
{
	static const char *const NAMED[] = { "cover", "folder", NULL };
	char dir[LIB_PATH * 2], only[256] = "";
	const char *p;
	DIR *d;
	struct dirent *e;
	int pictures = 0, k;
	bool found = false;

	if (al < 0 || al >= l->nalbums || l->albums[al].n <= 0) return false;
	p = l->tracks[l->albums[al].first].path;
	snprintf(dir, sizeof dir, "%s/%.*s", root, (int)dir_len(p), p);
	if (!(d = opendir(dir))) return false;
	while ((e = readdir(d)) && !found) {
		const char *dot = strrchr(e->d_name, '.');

		if (e->d_name[0] == '.' || !dot) continue;
		if (strcasecmp(dot, ".jpg") && strcasecmp(dot, ".jpeg") &&
		    strcasecmp(dot, ".png"))
			continue;
		for (k = 0; NAMED[k]; k++)
			if ((size_t)(dot - e->d_name) == strlen(NAMED[k]) &&
			    !strncasecmp(e->d_name, NAMED[k], strlen(NAMED[k])))
				found = true;
		if (found || pictures++ == 0)
			snprintf(only, sizeof only, "%s", e->d_name);
	}
	closedir(d);
	if (!found && pictures != 1) return false;
	return snprintf(out, n, "%s/%s", dir, only) < (int)n;
}

void ml_free(ml_lib *lib)
{
	free(lib->artists);
	free(lib->albums);
	free(lib->tracks);
	memset(lib, 0, sizeof *lib);
}

/* ---- the shelf's orders ----------------------------------------------------- */

static const struct { const char *name, *label, *book; } ORDERS[ML_ORDERS] = {
	[ML_BY_ARTIST] = { "artist", "Artist", "Author" },
	[ML_BY_ALBUM]  = { "album",  "Album",  "Title"  },
	[ML_BY_FOLDER] = { "folder", "Folder", "Folder" },
};

const char *ml_order_name(ml_order o)
{
	return ORDERS[(unsigned)o < ML_ORDERS ? o : ML_BY_ARTIST].name;
}

const char *ml_order_label(ml_order o, bool books)
{
	o = (unsigned)o < ML_ORDERS ? o : ML_BY_ARTIST;
	return books ? ORDERS[o].book : ORDERS[o].label;
}

ml_order ml_order_index(const char *name)
{
	int i;

	for (i = 0; name && i < ML_ORDERS; i++)
		if (!strcmp(ORDERS[i].name, name)) return (ml_order)i;
	return ML_BY_ARTIST;
}

/* An album as the album order compares it: its title, pointed at rather than
 * copied, and its number in the scan. The number is the tie-break, and it is
 * the artist one - the scan reads artists in order, so of two albums with the
 * same title the one whose artist comes first has the lower number. It is also
 * what keeps the order a function of the folder rather than of qsort. */
typedef struct { const char *album; int al; } shelf_key;

static int by_album(const void *a, const void *b)
{
	const shelf_key *x = a, *y = b;
	int c = strcasecmp(x->album, y->album);

	return c ? c : x->al - y->al;
}

/* An album as the folder order compares it: its folder path, with the album
 * name as tie-break. Two albums in the same folder stay together, and the
 * shelf reads as folders instead of artists. */
typedef struct { const char *folder; const char *album; int al; } folder_key;

static int by_folder(const void *a, const void *b)
{
	const folder_key *x = a, *y = b;
	int c = strcasecmp(x->folder, y->folder);

	if (c) return c;
	c = strcasecmp(x->album, y->album);
	return c ? c : x->al - y->al;
}

int ml_shelf_order(const ml_lib *l, ml_order by, bool books, int *out)
{
	shelf_key *k;
	folder_key *fk;
	int i, n = 0;

	for (i = 0; i < l->nalbums; i++)                  /* the scan's, by artist */
		if (l->albums[i].book == books) out[n++] = i;
	if (by == ML_BY_ALBUM) {
		if (n < 2) return n;
		k = malloc(sizeof *k * (size_t)n);
		if (!k) return n;
		for (i = 0; i < n; i++) {
			k[i].album = l->albums[out[i]].name;
			k[i].al    = out[i];
		}
		qsort(k, (size_t)n, sizeof *k, by_album);
		for (i = 0; i < n; i++) out[i] = k[i].al;
		free(k);
		return n;
	}
	if (by == ML_BY_FOLDER) {
		if (n < 2) return n;
		fk = malloc(sizeof *fk * (size_t)n);
		if (!fk) return n;
		for (i = 0; i < n; i++) {
			const char *p = l->tracks[l->albums[out[i]].first].path;
			const char *slash = strrchr(p, '/');
			fk[i].folder = slash ? p : "";
			fk[i].album  = l->albums[out[i]].name;
			fk[i].al     = out[i];
		}
		qsort(fk, (size_t)n, sizeof *fk, by_folder);
		for (i = 0; i < n; i++) out[i] = fk[i].al;
		free(fk);
		return n;
	}
	return n;
}
