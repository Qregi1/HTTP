Http_Server: Http_Server.c
	gcc Http_Server.c -o Http_Server -lpthread

.PHONY: clean
clean:
	rm Http_Server
