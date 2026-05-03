# Sample Data

These files are small inputs for manually demonstrating MultiSync without
depending on generated test data.

```bash
make
./multisync samples/repetitive.txt samples/natural.txt samples/alternating.txt
./multisync -d samples/repetitive.txt.rle
```

The `.rle` and `.out` files produced by these commands are ignored by Git.
