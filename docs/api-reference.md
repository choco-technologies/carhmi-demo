# carhmi API Reference

## Usage

```bash
carhmi [<view>] [<display>] [<music directory>]
```

| Argument | Meaning |
|----------|---------|
| `<view>` | The `.dmv` to show (default: found - `/eviews/carhmi_view/carhmi.dmv`, else `views/carhmi_view/carhmi.dmv` on any card in `/mnt`) |
| `<display>` | Name of the display (default: the first one dmview serves) |
| `<music directory>` | The MP3 files of the player (default: `music` on the card the view is on, else the first `/mnt/<card>/music`) |

carhmi waits for the view (up to 30 s - it may be on a card mounted after the
start) and for the display (up to 10 s), claims it for the view
(`libdmview_claim()`) and runs until it is stopped, then releases it: the
display's dmview returns to its own view.

Exit code: 0, `-ENOENT` (no view), `-ENODEV` (no display), `-ENOMEM`, or the
error of `libdmview_claim()`.

## The player

The MP3 files of the music directory (sorted by name, as many as there are)
are the player's queue: a list (dmlist) with a block of each file's name;
their ID3 tags - the title and the artist (the file's name when there is
none), the length - are read when a track is shown or played. A thread of
carhmi decodes the current one (dmmp3) and plays it through the audio
service (libdmaudio, on the first audio device); at its end the next one
starts.

The view shows twenty rows of the queue: a window over all the tracks, from
`first`. When the list is scrolled to its end (its start), the view asks for
more - the event `scroll`, +10 (-10) -, carhmi moves the window, and the view
scrolls its rows back by as many: what is seen stays where it is, and the
next tracks are below it.

Where the music was - the track (its file's name), the second in it, whether
it played - is kept in `.playing` next to the music
(`/mnt/<card>/music/.playing`, `playstate.c`): written at once when the track
or the playing changes, and every 15 seconds while it plays. At the next start
the same track goes on from there (paused, when it was), the window at it; a
file that is not there any more: the first track.

## The covers

The front cover of a track's ID3 tag (`dmmp3_find_picture()`; a baseline
JPEG or a PNG) is shown where the view's own image is - the playing track's
(`cover`) and the rows' (`rowN.cover`). The view (`data-dmod-src`) says what
to make of it: the sizes it draws a cover at, with their cuts, round corners
and blur (`cover.sizes`, `row0.cover.sizes`). carhmi (`covers.c`) decodes the
picture with dmimg - at the decoder's smallest scale, then averaged down as
far as the sizes let it - and makes each size with libtodmvi, into
`.covers/<key>-<size>.dmvi` next to the music; the key is a hash of the
picture, so the tracks of an album share their files. A cover made once is
read from the card the next time.

Two threads make them: one what is shown - the playing track, the rows of
the window - at the views' priority, as soon as it is asked for; the other
the rest of the tracks, one after another, only when nothing else wants the
CPU. `covers.done` of `covers.total` tracks are done; the view shows it
("Okładki 34/212") until they all are. A track without a picture, or with
one the decoders do not read (a progressive JPEG), keeps the view's image.

The decoders are loaded by name (`dmimg_jpeg`, `dmimg_png`) - they are not in
carhmi's dependencies: install them with it.

## The clock

The clock of the top bar is the RTC's: carhmi reads dmutc (`dmutc_get()`)
once a second and shows it as Polish time - UTC+1, UTC+2 from the last
Sunday of March to the last Sunday of October at 01:00 UTC (the EU rule;
the RTC keeps UTC). Without an RTC dmutc bound - no `dmutc@<rtc>` service
running, or its calendar not set - the clock is `--:--`.

## The volume

The loudspeaker in the top bar opens a panel under it: mute, - / + (ten
per cent a tap), the level; it closes three seconds after the last touch. The
view says what was done and carhmi does it (`volume.c`): the master volume of
the first audio device (`libdmaudio_device_set_volume()` and
`_device_set_mute()`), so it follows the music at once, track after track.
A new level turns the sound on again. The device may not be served yet when
carhmi starts: it is set as soon as it is.

The level is kept in `.volume` next to the music (`/mnt/<card>/music/.volume`),
written two seconds after the last change, and set again at the next start
(60 when there is none). Muting is not kept.

## The channel

carhmi and the view talk through the claim's channel (libdmview 0.10;
the scrolling needs dmview's SCROLLED, 0.12.1):

| Property | Set by | What it is |
|----------|--------|------------|
| `total` | carhmi | The tracks of the queue |
| `first`, `count` | carhmi | The window's first track, its rows that have one |
| `track` | carhmi | The current track's row in the window (-1: not in it) |
| `playing` | carhmi | 1 while it plays |
| `title`, `artist` | carhmi | Of the current track |
| `position`, `duration` | carhmi | Seconds |
| `progress` | carhmi | 0..1000, the width of the progress bar (`data-dmod-width`) |
| `rowN.title`, `.artist`, `.time` | carhmi | The twenty rows of the window |
| `clock` | carhmi | `HH:MM`, local time (`--:--`: not known) |
| `volume` | carhmi | 0..100, the master volume (`data-dmod-width`, scale 100) |
| `muted` | carhmi | 1 while the sound is off |
| `cover`, `rowN.cover` | carhmi | The image of the playing track, of a row: `<music>/.covers/<key>` (the view adds a size's end), else the view's own |
| `cover.sizes`, `row0.cover.sizes` | the view | The ends of the files' names of the sizes it shows a cover at (todmvs) |
| `covers.done`, `covers.total` | carhmi | Tracks whose covers are done, of all |

| Event | Value | What carhmi does |
|-------|-------|------------------|
| `toggle` | - | Play / pause (the first track when none was played) |
| `next`, `prev` | - | The next / previous track, around (the window follows it) |
| `select` | row | Plays the track of that row of the window |
| `scroll` | +n / -n | Moves the window by n tracks (as far as there are) |
| `volume` | 0..100 | Sets the volume (and turns the sound on) |
| `mute` | - | Mutes, or turns the sound on |
