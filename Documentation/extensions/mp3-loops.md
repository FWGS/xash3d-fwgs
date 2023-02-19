## Looping MP3 extension

It is now possible to loop MP3 file in Xash3D FWGS by adding a custom text tag with `LOOP_START` or `LOOPSTART` in description and time point (in raw samples) in value.

### Example with foobar2000
1. Open Foobar2000
2. Add your .mp3 file to playlist
3. Right click to newly added file and select Properties
4. In Metadata tab, at the bottom of the table, select "+add new"
5. In newly added line replace `«input field name»` with `LOOP_START` (without any symbols).
6. Press Tab and enter loop time point in raw samples. For example, `0` will replay sound file from beginning to end indefinitely.

### Possible alternatives
1. Classic WAV files looping. HQ WAV files can take too much disk space, and recommended software supporting cue points is paid, outdated and can't run on modern systems. (Although there is alternative that's proven to work with idTech-based engines called LoopAuditioneer.)
2. Vorbis looping through comment. Engine doesn't support Vorbis but this extension was highly inspired by this hack.

### Known bugs and limitations
1. At this time using MP3 as SFX requires complete decoding. This can cause noticeable stutters, so keep MP3 file length in mind.
2. We deliberately only support modern ID3v2.3 and ID3v2.4 tags. Using ID3v1 is not possible.



