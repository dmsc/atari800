
#include "a8pico.h"
#include "atari.h"
#include "util.h"
#include "log.h"
#include "memory.h"
#include "cpu.h"
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#include <stdlib.h>

#include "a8pico_os.h"
#include "a8pico_rom.h"

int A8Pico_enabled = FALSE;

struct DirItem {
    char type;
    char fname[32];
    char path[FILENAME_MAX];
};
static char A8Pico_dir[FILENAME_MAX];
static char sdcard_curdir[FILENAME_MAX];
static struct DirItem sdcard_dir[256];
static struct DirItem open_item = { 0 };
static int cart_page = 0;
static int cart_type = 0;
static int cart_state = 0;
static unsigned char d5page[256] = { 0x11 };
static unsigned char cart_ram[128*1024];

#if defined(HAVE_OPENDIR)
static int compare_fun(const void *a, const void *b)
{
    const struct DirItem *da = a;
    const struct DirItem *db = b;
    if( da->type != db->type )
        return db->type - da->type;
    return Util_stricmp(da->fname, db->fname);
}

static int A8Pico_read_dir(void)
{
    DIR *d;
    int n = 0;
    char path[FILENAME_MAX];
    memset(sdcard_dir, 0, sizeof(sdcard_dir));
    Util_catpath(path, A8Pico_dir, sdcard_curdir);
    d = opendir(path);
    if (d)
    {
        while( n < 255 )
        {
            struct DirItem *out = &sdcard_dir[n];
            struct dirent *dir = readdir(d);
            if (!dir)
                break;
            if (dir->d_name[0] == '.')
                continue;
            if (strlen(dir->d_name) > 32)
            {
                Log_print("A8PicoCart: filename '%s' too long, skipping", dir->d_name);
                continue;
            }
            Util_catpath(out->path, path, dir->d_name);
            if (Util_direxists(out->path))
                out->type = 1;
            else if (Util_fileexists(out->path))
                out->type = 0;
            else {
                Log_print("A8PicoCart: invalid file '%s', skipping", dir->d_name);
                continue;
            }
            Util_strlcpy(out->fname, dir->d_name, 32);
            n++;
        }
        closedir(d);
    }
    /* Sort entries */
    qsort(&sdcard_dir[0], n, sizeof(sdcard_dir[0]), compare_fun);
    return n;
}

/* emulate strcasestr */
static int rec_strcasestr(char *str, char *pat)
{
    char *s = str;
    size_t plen = strlen(pat);

    /* Search pat on any position of str */
    while( *s && Util_strnicmp(s, pat, plen))
        s++;
    if (*s || !*pat)
        return 0;
    else
        return 1;
}

static int rec_search(char *cdir, char *srch, int n)
{
    DIR *d;
    char path[FILENAME_MAX];

    if (n >= 255)
        return n;

    Util_catpath(path, A8Pico_dir, cdir);
    d = opendir(path);
    if (d)
    {
        while( n < 255 )
        {
            struct DirItem *out = &sdcard_dir[n];
            struct dirent *dir = readdir(d);
            if (!dir)
                break;
            if (dir->d_name[0] == '.')
                continue;
            if (strlen(dir->d_name) > 32)
                continue;
            Util_catpath(out->path, path, dir->d_name);
            if (Util_direxists(out->path)) {
                /* recurse */
                char rdir[FILENAME_MAX];
                Util_catpath(rdir, cdir, dir->d_name);
                n = rec_search(rdir, srch, n);
            }
            else if (Util_fileexists(out->path) && !rec_strcasestr(dir->d_name, srch))
            {
                out->type = !Util_stricmp(dir->d_name, srch);
                Util_strlcpy(out->fname, dir->d_name, 32);
                n++;
            }
        }
        closedir(d);
    }
    return n;
}

static int A8Pico_search_dir(char *srch)
{
    int n, i;
    memset(sdcard_dir, 0, sizeof(sdcard_dir));
    n = rec_search("", srch, 0);
    /* Sort entries */
    qsort(&sdcard_dir[0], n, sizeof(sdcard_dir[0]), compare_fun);
    /* Reset "score" back to 0 */
    for (i = 0; i < n; i++)
        sdcard_dir[i].type = 0;
    return n;
}
#else
static int A8Pico_read_dir()
{
    return 0;
}
static int A8Pico_search_dir(char *srch)
{
    return 0;
}
#endif

static void MapA8PicoCart(void)
{
    Log_print("A8PicoCart mapped");
    MEMORY_Cart809fDisable();
    MEMORY_CartA0bfEnable();
    MEMORY_CopyFromCart(0xa000, 0xbfff, a8pico_rom);
}

static void UnmapA8PicoCart(void)
{
    Log_print("A8PicoCart unmapped");
    MEMORY_Cart809fDisable();
    MEMORY_CartA0bfDisable();
}

int A8Pico_Initialise(int *argc, char *argv[])
{
    int i, j;
    int help_only = FALSE;
    for (i = j = 1; i < *argc; i++) {
        if (strcmp(argv[i], "-a8pico") == 0) {
            if (i + 1 < *argc) {
                A8Pico_enabled = TRUE;
                Util_strlcpy(A8Pico_dir, argv[++i], sizeof(A8Pico_dir));
            } else {
                Log_print("Missing argument for '%s'", argv[i]);
                return FALSE;
            }
        } else {
            if (strcmp(argv[i], "-help") == 0) {
                help_only = TRUE;
                Log_print("\t-a8pico          Emulate A8PicoCart cartridge");
            }
            argv[j++] = argv[i];
        }
    }
    *argc = j;

    if (help_only)
        return TRUE;

    if (A8Pico_enabled) {
        Log_print("A8PicoCart enabled");
    }

    return TRUE;
}

void A8Pico_InsertCart()
{
    MapA8PicoCart();
}

void A8Pico_Exit(void)
{
}

int A8Pico_D5GetByte(UWORD addr, int no_side_effects)
{
    int ad = addr & 0xFF;
    if (cart_state == 0)
    {
        int byte = d5page[ad];
        Log_print("A8PicoCart read from %02x = %02x", addr & 0xFF, byte);
        return byte;
    }
    else if (cart_state == 2)
    {
        int caddr = (cart_page * 256 + ad) & 0x1FFFF;
        int byte = cart_ram[caddr];
        Log_print("A8PicoCart XEX read from %05x = %02x", caddr, byte);
        return byte;
    }
    else if (cart_state == 3)
    {
        return d5page[ad];
    }
    else
        Log_print("A8PicoCart error, cant read %02x", addr & 0xFF);
    return 0xFF;
}

void A8Pico_D5PutByte(UWORD addr, UBYTE byte)
{
    int ad = addr & 0xFF;
    if (cart_state == 2)
    {
        /* XEX Loader */
        if( ad == 0 )
            cart_page = (cart_page & 0xFF00) | byte;
        else if( ad == 1 )
            cart_page = (cart_page & 0x00FF) | byte;
        else
            Log_print("A8PicoCart bad write %02x to %02x", byte, ad);
        return;
    }
    else if (cart_state == 3)
    {
        /* ATR */
        d5page[ad] = byte;
        if( ad != 0xDF )
            return;
        switch(byte)
        {
#if 0 /* Unused in firmware */
            case 0x20: /* CART_CMD_MOUNT_ATR */
                break;
#endif
            case 0x21: /* CART_CMD_READ_ATR_SECTOR */
            {
                int sect = d5page[1] + 256 * d5page[2];
                int offs = d5page[3];
                FILE *atr;
                Log_print("A8PicoCart read sector %d", sect);
                d5page[1] = 1;
                if( NULL != (atr = fopen(open_item.path, "rb")) )
                {
                    if( sect < 4 )
                    {
                        if( !fseek(atr, 16 + (sect - 1) * 128, SEEK_SET) )
                            d5page[1] = (1 != fread(d5page + 2, 128, 1, atr));
                    }
                    else
                    {
                        if( !fseek(atr, 16 + (sect - 1) * 128, SEEK_SET) )
                            d5page[1] = (1 != fread(d5page + 2, 128, 1, atr));
                    }
                    fclose(atr);
                }
                break;
            }
            case 0x22: /* CART_CMD_WRITE_ATR_SECTOR */
            {
                int sect = d5page[1] + 256 * d5page[2];
                Log_print("A8PicoCart write sector %d", sect);
                d5page[1] = 4;
                break;
            }
            case 0x23: /* CART_CMD_ATR_HEADER */
                {
                    d5page[1] = 1;
                    FILE *atr = fopen(open_item.path, "rb");
                    if( atr )
                    {
                        d5page[1] = (1 != fread(d5page + 2, 16, 1,atr));
                        fclose(atr);
                    }
                }
                break;
            default:
                Log_print("A8PicoCart unknown ATR command %02x", byte);
                break;
        }
        d5page[0] = 0x11;
        return;
    }

    Log_print("A8PicoCart write %02x to %02x", byte, ad);
    d5page[ad] = byte;
    /* Cart Command */
    if( ad == 0xDF )
    {
        switch(byte)
        {
            case 0x0: /* CART_CMD_OPEN_ITEM */
            {
                memcpy(&open_item, &sdcard_dir[d5page[0]], sizeof(open_item));
                if (!open_item.fname[0])
                {
                    d5page[1] = 4;
                }
                else if (open_item.type)
                {
                    char newdir[FILENAME_MAX];
                    Util_catpath(newdir, sdcard_curdir, open_item.fname);
                    memcpy(sdcard_curdir, newdir, FILENAME_MAX);
                    d5page[1] = 0; /* DIR CHANGED */
                }
                else
                {
                    /* Open file and check type: */
                    FILE *f = fopen(open_item.path, "rb");
                    if( NULL != f )
                    {
                        int c1 = getc(f);
                        int c2 = getc(f);
                        int c3 = getc(f);
                        int c4 = getc(f);
                        if ( c1 == 0xFF && c2 == 0xFF )
                        {
                            int len;
                            d5page[1] = 2; /* XEX */
                            cart_type = -2;
                            fseek(f, 0, SEEK_SET);
                            len = fread(cart_ram + 4, 1, sizeof(cart_ram) - 4, f);
                            cart_ram[0] = len;
                            cart_ram[1] = len >> 8;
                            cart_ram[2] = len >> 16;
                            cart_ram[3] = len >> 24;
                        }
                        else if ( c1 == 0x96 && c2 == 0x02 )
                        {
                            cart_type = -3;
                            d5page[1] = 3; /* ATR */
                        }
                        else if ( c1 == 'C' && c2 == 'A' && c3 == 'R' && c4 == 'T')
                        {
                            d5page[1] = 1; /* CAR */
                        }
                        else
                        {
                            static const char msg[] = "Invalid File Type";
                            memcpy(d5page + 2, msg, sizeof(msg));
                            d5page[1] = 4;
                        }
                        fclose(f);
                    }
                    else
                    {
                        static const char msg[] = "Can't open file";
                        memcpy(d5page + 2, msg, sizeof(msg));
                        d5page[1] = 4;
                    }
                }
                break;
            }
            case 0x1: /* CART_CMD_READ_CUR_DIR */
                d5page[1] = 0;
                d5page[2] = A8Pico_read_dir();
                break;
            case 0x2: /* CART_CMD_GET_DIR_ENTRY */
                d5page[1] = sdcard_dir[d5page[0]].type;
                memcpy(d5page+2, sdcard_dir[d5page[0]].fname, 32);
                break;
            case 0x3: /* CART_CMD_UP_DIR */
            {
                char newdir[FILENAME_MAX];
                Util_splitpath(sdcard_curdir, newdir, 0);
                memcpy(sdcard_curdir, newdir, FILENAME_MAX);
                break;
            }
            case 0x4: /* CART_CMD_ROOT_DIR */
                memset(sdcard_curdir, 0, FILENAME_MAX);
                break;
            case 0x5: /* CART_CMD_SEARCH */
            {
                char srch[33];
                memcpy(srch, d5page, 32);
                srch[32] = 0;
                d5page[1] = 0;
                d5page[2] = A8Pico_search_dir(srch);
                break;
            }
            case 0x10: /* CART_CMD_LOAD_SOFT_OS */
                d5page[1] = 0; /* ok */
                break;
            case 0x11: /* CART_CMD_SOFT_OS_CHUNK */
                memcpy(d5page+1, a8pico_os_rom + 128 * (0x7F & d5page[0]), 128);
                break;
            case 0xF0: /* CART_CMD_RESET_FLASH */
                break;
            case 0xFE: /* CART_CMD_NO_CART */
                break;
            case 0xFF: /* CART_CMD_ACTIVATE_CART */
                /* depending on cart type, activate different functions */
                if( cart_type < 0 )
                {
                    cart_state = -cart_type;
                    UnmapA8PicoCart();
                }
                break;
            default:
                Log_print("A8PicoCart unknown command %02x", byte);
                break;
        }
        d5page[0] = 0x11;
    }
    /*...*/
}
