#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <cjson/cJSON.h>

#define MAX_SCREENS 16
#define MAX_DESKTOPS 64
#define MSG_SIZE 4096
#define LINE_SIZE 8192

typedef struct {
    int number;
    int is_current;
    int is_urgent;
    int num_of_clients;
} desktop_t;

typedef struct {
    char name[256];
    char out[MSG_SIZE];
    desktop_t desktops[MAX_DESKTOPS];
    int desktop_count;
    char current_client[512];
    int randr_order;
    char desktop_mode[64];
} screen_t;


static screen_t screens[MAX_SCREENS];
static int screen_count = 0;
static char clock_line[1024] = "";
static char global_desktop_mode[64] = "";
static char global_current_screen[256] = "";

static screen_t *find_or_create_screen(const char *name)
{
    for (int i = 0; i < screen_count; i++) {
        if (strcmp(screens[i].name, name) == 0)
            return &screens[i];
    }
    if (screen_count >= MAX_SCREENS)
        return NULL;
    screen_t *s = &screens[screen_count++];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    return s;
}

static int cmp_desktops(const void *a, const void *b)
{
    return ((const desktop_t *)a)->number - ((const desktop_t *)b)->number;
}

static void build_message(screen_t *s)
{
    char msg[MSG_SIZE];
    int pos = 0;
    int current_clients = 0;

    pos += snprintf(msg + pos, sizeof(msg) - pos, "%%{Sn%s}", s->name);

    qsort(s->desktops, s->desktop_count, sizeof(desktop_t), cmp_desktops);

    for (int i = 0; i < s->desktop_count; i++) {
        desktop_t *d = &s->desktops[i];
        if (d->is_current) {
            pos += snprintf(msg + pos, sizeof(msg) - pos,
                           "|%%{B#39c488} %d %%{B-}", d->number);
            current_clients = d->num_of_clients;
        } else if (d->is_urgent) {
            pos += snprintf(msg + pos, sizeof(msg) - pos,
                           "|%%{B#FF0000} %d %%{B-}", d->number);
        } else if (d->num_of_clients > 0) {
            pos += snprintf(msg + pos, sizeof(msg) - pos,
                           "|%%{B#004C98} %d %%{B-}", d->number);
        }
    }

    pos += snprintf(msg + pos, sizeof(msg) - pos,
                   "%%{F#FF00FF}|%%{F-}"
                   "%%{B#7F783E}[CS:%s][Scr:%s][N:%d][A:%d][L:%s]%%{B-}",
                   global_current_screen,
                   s->name, s->randr_order,
                   current_clients,
                   s->desktop_mode);

    if (s->current_client[0] != '\0') {
        pos += snprintf(msg + pos, sizeof(msg) - pos,
                       "%%{c}%%{U#00FF00}%%{+u}%%{+o}%%{B#AC59FF}%%{F-}"
                       "        %s        %%{-u}%%{-o}%%{B-}",
                       s->current_client);
    }

    snprintf(s->out, sizeof(s->out), "%s", msg);
    size_t out_len = strlen(s->out);
    size_t cl_len = strlen(clock_line);
    if (out_len + cl_len < sizeof(s->out))
        memcpy(s->out + out_len, clock_line, cl_len + 1);
}

static void process_json(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root)
        return;

    cJSON *dm = cJSON_GetObjectItem(root, "desktop_mode");
    if (dm && cJSON_IsString(dm))
        strncpy(global_desktop_mode, dm->valuestring,
                sizeof(global_desktop_mode) - 1);

    cJSON *cs = cJSON_GetObjectItem(root, "current_screen");
    if (cs && cJSON_IsString(cs))
        strncpy(global_current_screen, cs->valuestring,
                sizeof(global_current_screen) - 1);

    cJSON *scr_obj = cJSON_GetObjectItem(root, "screens");
    if (!scr_obj || !cJSON_IsObject(scr_obj)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *scr;
    cJSON_ArrayForEach(scr, scr_obj) {
        screen_t *s = find_or_create_screen(scr->string);
        if (!s)
            continue;
        snprintf(s->desktop_mode, sizeof(s->desktop_mode), "%s",
                 global_desktop_mode);

        s->randr_order = 0;
        cJSON *ro = cJSON_GetObjectItem(scr, "randr_order");
        if (ro && cJSON_IsNumber(ro))
            s->randr_order = ro->valueint;

        s->current_client[0] = '\0';
        cJSON *cc = cJSON_GetObjectItem(scr, "current_client");
        if (cc && cJSON_IsString(cc))
            strncpy(s->current_client, cc->valuestring,
                    sizeof(s->current_client) - 1);

        s->desktop_count = 0;
        cJSON *desktops = cJSON_GetObjectItem(scr, "desktops");
        if (desktops && cJSON_IsObject(desktops)) {
            cJSON *desk;
            cJSON_ArrayForEach(desk, desktops) {
                if (s->desktop_count >= MAX_DESKTOPS)
                    break;
                desktop_t *d = &s->desktops[s->desktop_count++];
                cJSON *n = cJSON_GetObjectItem(desk, "number");
                d->number = (n && cJSON_IsNumber(n)) ? n->valueint : 0;
                cJSON *ic = cJSON_GetObjectItem(desk, "is_current");
                d->is_current = (ic && cJSON_IsTrue(ic)) ? 1 : 0;
                cJSON *nc = cJSON_GetObjectItem(desk, "number_of_clients");
                d->num_of_clients = (nc && cJSON_IsNumber(nc)) ? nc->valueint : 0;
                cJSON *iu = cJSON_GetObjectItem(desk, "is_urgent");
                d->is_urgent = (iu && cJSON_IsTrue(iu)) ? 1 : 0;
            }
        }

        build_message(s);
        printf("%s\n", s->out);
        fflush(stdout);
    }

    cJSON_Delete(root);
}

static void parse_xrandr_output(const char *output)
{
    regex_t re;
    regmatch_t match[1];

    if (regcomp(&re, "\\S+$", REG_EXTENDED) != 0) {
        fprintf(stderr, "Failed to compile regex\n");
        exit(1);
    }

    char *line = strtok((char *)output, "\n");
    int first = 1;
    while (line) {
        if (first) {
            first = 0;
            line = strtok(NULL, "\n");
            continue;
        }
        if (regexec(&re, line, 1, match, 0) == 0) {
            int len = match[0].rm_eo - match[0].rm_so;
            char name[256];
            strncpy(name, line + match[0].rm_so, len);
            name[len] = '\0';
            find_or_create_screen(name);
        }
        line = strtok(NULL, "\n");
    }

    regfree(&re);
}

int main(void)
{
    const char *fifo_name = getenv("FVWM3_STATUS_PIPE");
    if (!fifo_name) {
        fprintf(stderr, "fifo defaulting to /tmp/fvwm3.pipe...\n");
        fifo_name = "/tmp/fvwm3.pipe";
    }

    FILE *pipe = fopen(fifo_name, "r");
    if (!pipe) {
        perror("Failed to open FIFO");
        return 1;
    }

    FILE *rp = popen("xrandr --listactivemonitors", "r");
    if (rp) {
        char buf[4096];
        size_t total = 0;
        char *xrandr_out = malloc(1);
        xrandr_out[0] = '\0';
        while (fgets(buf, sizeof(buf), rp)) {
            size_t len = strlen(buf);
            xrandr_out = realloc(xrandr_out, total + len + 1);
            memcpy(xrandr_out + total, buf, len);
            total += len;
            xrandr_out[total] = '\0';
        }
        pclose(rp);
        if (total > 0)
            parse_xrandr_output(xrandr_out);
        free(xrandr_out);
    }

    if (screen_count == 0) {
        fprintf(stderr, "No active screens found\n");
        fclose(pipe);
        return 1;
    }

    char line[LINE_SIZE];
    while (fgets(line, sizeof(line), pipe)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        if (strncmp(line, "clock:", 6) == 0) {
            size_t cl_src_len = strlen(line + 6);
            if (cl_src_len >= sizeof(clock_line))
                cl_src_len = sizeof(clock_line) - 1;
            memcpy(clock_line, line + 6, cl_src_len);
            clock_line[cl_src_len] = '\0';
        } else {
            process_json(line);
        }
    }

    fclose(pipe);
    return 0;
}
