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

| Event | Value | What carhmi does |
|-------|-------|------------------|
| `toggle` | - | Play / pause (the first track when none was played) |
| `next`, `prev` | - | The next / previous track, around (the window follows it) |
| `select` | row | Plays the track of that row of the window |
| `scroll` | +n / -n | Moves the window by n tracks (as far as there are) |
| `volume` | 0..100 | Sets the volume (and turns the sound on) |
| `mute` | - | Mutes, or turns the sound on |
