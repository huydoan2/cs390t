#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <number of elements> [file suffix]\n", argv[0]);
        return -1;
    }

    int n = atoi(argv[1]);
    char suffix[16];
    if (argc >= 3) {
        suffix[0] = '_';
        strcpy(&suffix[1], argv[2]);
    }
    else
        suffix[0] = '\0';
    char filename[32];
    FILE *fp;

    // TODO: generate increasing sequence
    sprintf(filename, "inc%s.txt", suffix);
    printf("Generating increasing sequence to %s\n", filename);
    fp = fopen(filename, "w");
    fprintf(fp, "%d\n", n);
    for (int i = 0; i < n; i++)
        fprintf(fp, "%d%c", i, i==n-1?'\n':' ');
    fclose(fp);

    // TODO: generate decreasing sequence
    sprintf(filename, "dec%s.txt", suffix);
    printf("Generating decreasing sequence to %s\n", filename);
    fp = fopen(filename, "w");
    fprintf(fp, "%d\n", n);
    for (int i = n-1; i >= 0; i--)
        fprintf(fp, "%d%c", i, i==0?'\n':' ');
    fclose(fp);

    // TODO: generate interleaving sequence
    sprintf(filename, "itl%s.txt", suffix);
    printf("Generating interleaving sequence to %s\n", filename);
    fp = fopen(filename, "w");
    fprintf(fp, "%d\n", n);
    for (int i = 0; i < n; i++)
        fprintf(fp, "%d%c", i&1?(n-1-(i>>1)):i>>1 ,i==n-1?'\n':' ');
    fclose(fp);

    // TODO: generate random sequence
    sprintf(filename, "rnd%s.txt", suffix);
    printf("Generating random sequence to %s\n", filename);
    fp = fopen(filename, "w");
    fprintf(fp, "%d\n", n);
    srand(time(NULL));
    for (int i = 0; i < n; i++)
        fprintf(fp, "%d%c", rand()%n, i==n-1?'\n':' ');
    fclose(fp);

    return 0;
}
