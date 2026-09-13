# Remote source adapters

Remote sources use the small C interface in `src/adapters/adapter.h`. The sync engine only knows how to
connect, list tracks and playlists, download a track, and disconnect. HTTP, authentication, pagination,
and source-specific JSON stay inside the adapter.

To add an adapter:

1. Add `src/adapters/your_source.c`.
2. Export `const remote_adapter_t your_source_adapter` from that file.
3. Add `ADAPTER(your_source)` to `src/adapters/adapters.def`.
4. Build and add focused tests for its protocol behavior.

The CMake build automatically includes C files in `src/adapters`. The name in `adapters.def`, the exported
symbol prefix, and `remote_adapter_t.name` must match. Users select it with:

```text
audiosync profile add Player --serial ID --url https://server \
  --username USER --adapter your_source
```

## Contract

- `connect` receives the saved URL, username, and password. On success it stores an adapter-owned session
  in `*session`; `disconnect` must release it.
- Set `requires_username` when the CLI must reject profiles with an empty username.
- `list_tracks` returns a heap-allocated `remote_track_t` array and count.
- `list_playlists` returns a heap-allocated `remote_playlist_t` array. Each `items` array is also heap allocated.
- AudioSync releases those arrays with `remote_tracks_free` and `remote_playlists_free`, so allocate them
  with `malloc`, `calloc`, or `realloc`.
- `download` writes the complete track to `path`, returns zero on success, and reports byte progress through
  the optional callback.
- Every callback returns zero on success and a nonzero value on failure.

Keep adapter-specific types and helpers private to its `.c` file. The built-in AudioStream adapter in
`src/adapters/audiostream.c` is the reference implementation. Existing configurations using the old
`remote` source type are migrated to `audiostream` when loaded.
