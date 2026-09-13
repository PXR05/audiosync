#include "adapters/adapter.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const remote_adapter_t *adapter = remote_adapter_find("audiostream");
    assert(adapter && !strcmp(adapter->name, "audiostream"));
    assert(remote_adapter_find("remote") == adapter);
    assert(!remote_adapter_find("missing"));

    remote_track_t *tracks = calloc(1, sizeof *tracks);
    remote_playlist_t *playlists = calloc(1, sizeof *playlists);
    assert(tracks && playlists);
    playlists[0].items = calloc(1, sizeof *playlists[0].items);
    assert(playlists[0].items);
    remote_tracks_free(tracks);
    remote_playlists_free(playlists, 1);
    puts("PASS: adapter lookup, legacy alias, and shared ownership");
    return 0;
}
