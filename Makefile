all: wcc jcp

wcc: wcc.c
	cc -o wcc wcc.c

jcp: jcp.c
	cc -o jcp jcp.c


clean:
	rm wcc jcp


.PHONY = all clean
