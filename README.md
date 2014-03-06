process-itw
===========

This project is used to convert ITW image files to PNG

see http://www.reddit.com/r/ReverseEngineering/comments/1zfepb/reverse_engineering_an_unknown_image_format_itw/

Building & running requires libgd

Building: 
```
gcc process-itw.c -o process-itw -std=c99 -lgd
```

Running:
```
./process-itw file1.ITW [ file2.ITW ... ]
```

Output is written to a file called the same as the input, but with .png appended.


