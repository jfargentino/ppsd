
#include <stdio.h>
#include <math.h>

double durbin_watson(double *residuals, int n) {
    if (n <= 1) return 0.0;  // Pas assez de données

    double sum_diff_sq = 0.0;
    double sum_sq = 0.0;

    for (int i = 0; i < n; i++) {
        sum_sq += residuals[i] * residuals[i];
    }
    for (int i = 1; i < n; i++) {
        double diff = residuals[i] - residuals[i-1];
        sum_diff_sq += diff * diff;
    }

    if (sum_sq == 0.0) return 0.0;  // Évite la division par zéro
    return sum_diff_sq / sum_sq;
}

int main() {
    // Exemple : Résidus simulés (à remplacer par vos données)
    double residuals[] = {10.0, -5.0, 12.0, -8.0, 15.0};
    int n = sizeof(residuals) / sizeof(residuals[0]);

    double d = durbin_watson(residuals, n);
    printf("Durbin-Watson d = %.4f\n", d);

    if (d < 1.5) {
        printf("→ Autocorrélation positive : augmentez Q[1][1] ou réduisez R.\n");
    } else if (d > 2.5) {
        printf("→ Autocorrélation négative : réduisez Q[1][1] ou augmentez R.\n");
    } else {
        printf("→ Pas d'autocorrélation : paramètres Q et R bien réglés !\n");
    }

    return 0;
}

