#include "adapters/adapter.h"
#include "adapters/builtin.h"
#include <stdlib.h>
#include <string.h>

static const remote_adapter_t *const adapters[] = {
#define ADAPTER(name) &name##_adapter,
#include "adapters/adapters.def"
#undef ADAPTER
};

const remote_adapter_t *remote_adapter_find(const char *name) {
    if (name && !strcmp(name, "remote"))
        name = "audiostream";
    if (!name)
        return NULL;
    for (size_t i = 0; i < sizeof adapters / sizeof *adapters; ++i)
        if (!strcmp(name, adapters[i]->name))
            return adapters[i];
    return NULL;
}

void remote_tracks_free(remote_track_t *tracks) {
    free(tracks);
}

void remote_playlists_free(remote_playlist_t *playlists, int count) {
    for (int i = 0; i < count; ++i)
        free(playlists[i].items);
    free(playlists);
}
