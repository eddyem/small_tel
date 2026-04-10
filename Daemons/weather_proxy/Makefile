CC = gcc
CFLAGS = -Wall -Wextra -fPIC -DEBUG
LDFLAGS = -lrt -pthread -lusefull_macros

all: weather_daemon libweather.so weather_clt_example

weather_daemon: weather_daemon.o
	$(CC) -o $@ $^ $(LDFLAGS)

weather_clt_example: weather_clt_example.o
	$(CC) -o $@ $^ $(LDFLAGS) -l weather -L.

weather_daemon.o: weather_daemon.c weather_data.h
	$(CC) $(CFLAGS) -c $<

libweather.so: weather_client.o
	$(CC) -shared -o $@ $^ $(LDFLAGS)

#libweather.a: weather_client.o
#	ar rcs $@ $^

weather_client.o: weather_client.c weather_data.h
	$(CC) $(CFLAGS) -c $<

weather_clt_example.o: weather_clt_example.c libweather.so
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o weather_daemon libweather.so libweather.a weather_clt_example

install:
	cp libweather.so /usr/local/lib/
	cp weather_data.h /usr/local/include/