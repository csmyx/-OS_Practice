s: mem
	./$<

mem: mem.c
	gcc $< -o $@


clean:
	rm -f mem
