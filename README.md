Minimal example of an ffmpeg video filter which applies a pair of OpenGL shaders
to each frame of its input, and emits the shaded frames.

## Building

```sh
~/FFmpeg$ ln -s ~/ffmpeg-opengl/vf_gearstitch.c libavfilter/
~/FFmpeg$ git apply ~/ffmpeg-opengl/FFmpeg.diff
~/FFmpeg$ ./configure  --enable-libx264 --enable-filter=genericshader \
          --enable-gpl --enable-opengl --extra-libs='-lGLEW -lglfw'
~/FFmpeg$ make
```

- You may want to pass `--cc=clang` on OS X.
- There may slight variation in how [GLEW](http://glew.sourceforge.net) and [glfw](http://http://www.glfw.org/) are named (with regard to`--extra-libs`, above), e.g. `-lglew` or `-lglfw3` - check `pkg-config`.
- The above example builds a minimal FFmpeg binary with libx264, which you'll need to build/install if that's the route you go.  There's nothing codec-specific about the filter itself.

## Running

```sh
./ffmpeg -i input.mp4 -vf genericshader -y output.mp4
```

## License

ffmpeg-opengl is free and unencumbered public domain software. For more
information, see http://unlicense.org/ or the accompanying UNLICENSE
file.
