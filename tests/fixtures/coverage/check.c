int calculate(int value);
int main(int argc, char **argv) {
    (void)argv;
    if (argc > 1) return 1;
    return calculate(3) == 3 ? 0 : 1;
}
