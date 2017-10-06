All of the assets here are compressed with zlib and #include'd as code in the project.

For png files, the data is first converted into raw rgb / rgba data with GraphicMagick:
```
gm convert image.png image.rgb
```

The data is then compressed with:
```
openssl zlib -e < image.rgb > image.gz
```

The data is finally transformed into a C-style array with:
```
xxd -i image.gz
```

Note, the width, height and uncompressed sizes are all filled in manually alongside the xxd output.
