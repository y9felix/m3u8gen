# m3u8gen

Fast ID3-aware extended M3U playlist generator written in C99.

`m3u8gen` scans audio files in directories, parses ID3v2 metadata (artist, title, track length), and generates standard `#EXTM3U` playlists suitable for media players like `mpv`.

Features:
- Fast directory traversal and parsing
- Parses ID3v2.3 and ID3v2.4 tags natively
- Extracts accurate stream duration and metadata
- Pure C99 with zero external library dependencies

## Requirements

- C99 compiler (`cc`)
- GNU Make

## Installation

```sh
make
make install
```

## Usage

```sh
m3u8gen /path/to/music > playlist.m3u8
```

## License

MIT License.
