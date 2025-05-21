all: KV1_log KV1_s1 KV1_s2 KV1_cli

KV1_log: KV1_log.c
	gcc KV1_log.c -o log -lpthread

KV1_s1: KV1_s1.c
	gcc KV1_s1.c -o s1 -lpthread

KV1_s2: KV1_s2.c
	gcc KV1_s2.c -o s2 -lpthread

KV1_cli: KV1_cli.c
	gcc KV1_cli.c -o cli `pkg-config --cflags --libs gtk+-3.0` -lpthread

clean:
	rm -f KV1_log KV1_s1 KV1_s2 KV1_cli
