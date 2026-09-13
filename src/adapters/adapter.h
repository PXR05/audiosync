#ifndef AUDIOSYNC_ADAPTER_H
#define AUDIOSYNC_ADAPTER_H

#include <wchar.h>

typedef void (*remote_progress_cb)(unsigned long long done, unsigned long long total, void *context);

typedef struct {
    char id[64];
    char filename[128];
    char title[256];
    char artist[256];
    char album[256];
    long long size;
    char uploaded_at[40];
} remote_track_t;

typedef struct {
    int position;
    remote_track_t track;
} remote_playlist_item_t;

typedef struct {
    char id[128];
    char name[256];
    remote_playlist_item_t *items;
    int count;
} remote_playlist_t;

typedef struct {
    const char *url;
    const char *username;
    const char *password;
} remote_source_t;

typedef struct remote_adapter {
    const char *name;
    int requires_username;
    int (*connect)(const remote_source_t *source, void **session);
    int (*list_tracks)(void *session, remote_track_t **tracks, int *count);
    int (*list_playlists)(void *session, remote_playlist_t **playlists, int *count);
    int (*download)(void *session, const char *track_id, const wchar_t *path, remote_progress_cb progress,
                    void *context);
    void (*disconnect)(void *session);
} remote_adapter_t;

const remote_adapter_t *remote_adapter_find(const char *name);
void remote_tracks_free(remote_track_t *tracks);
void remote_playlists_free(remote_playlist_t *playlists, int count);

#endif
