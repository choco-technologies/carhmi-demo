Place the Polish female voice MP3 files here, using the exact filenames from
`docs/games/elevenlabs.csv`. The recordings are optional build inputs and are
packaged into `views/<screen>/literki/audio/` with the game's pictures.

Speech is not synthesized by the STM32. Before recordings are added, the board
still plays the generated reward chime and applause. Browser preview can use a
locally installed Polish speech-synthesis voice as a fallback.
