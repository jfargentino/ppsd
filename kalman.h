#ifndef KALMAN_H
#define KALMAN_H

// Structure du filtre de Kalman
struct kalman_t {
    double x[2];       // État : [offset (ns), drift (ppb)]
    double P[2][2];    // Matrice de covariance
    double F[2][2];    // Matrice de transition
    double H[2];       // Matrice d'observation
    double Q[2][2];    // Covariance du bruit de processus
    double R;          // Covariance du bruit de mesure
    //double dt;         // Intervalle de temps (1.0 s pour PPS)
};

// Initialisation du filtre
void kalman_init(struct kalman_t * kf,
                 double initial_offset,
                 double initial_drift,
                 double Q_offset,
                 double Q_drift,
                 double R);

// Prédiction
double kalman_predict(struct kalman_t * kf, double * drift_ppb);

// Mise à jour (correction)
double kalman_update(struct kalman_t * kf,
                     double offset_ns,
                     double * drift_ppb);

#endif // KALMAN_H
