
volatile int signal_to_die = 0;

void sigma(int c) {
    (void)c;

    signal_to_die++;
}

