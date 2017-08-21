PROJ := main

all: $(PROJ)

CFLAGS := -I/opt/vc/include -pipe -W -Wall -Wextra -g -O2
LDLIBS := -L/opt/vc/lib

$(PROJ): $(PROJ).o
$(PROJ): LDLIBS += -lbcm_host -lvcos -lvchiq_arm -pthread -lmmal_core -lmmal_util -lmmal_vc_client

.PHONY: clean
clean:
	$(RM) $(PROJ)
	$(RM) $(PROJ).o
