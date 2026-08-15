#include "kalman.h"
#include <stddef.h>

// Initialisation du filtre
void kalman_init(struct kalman_t * kf,
                 double initial_offset,
                 double initial_drift,
                 double Q_offset,
                 double Q_drift,
                 double R) {

    kf->x[0] = initial_offset;  // Offset initial (ns)
    kf->x[1] = initial_drift;   // Drift initial (ppb)

    // Matrice de covariance initiale (incertitude élevée)
    kf->P[0][0] = 1e12l;  // Incertitude initiale sur offset (ns²)
    kf->P[0][1] = 0.0l;
    kf->P[1][0] = 0.0l;
    kf->P[1][1] = 1e12l;  // Incertitude initiale sur drift (ppb²)

    // Matrice de transition (F)
    kf->F[0][0] = 1.0l;
    kf->F[0][1] = 1.0l;  // dt = 1.0 s → offset += drift * 1
    kf->F[1][0] = 0.0l;
    kf->F[1][1] = 1.0l;

    // Matrice d'observation (H)
    kf->H[0] = 1.0l;
    kf->H[1] = 0.0l;

    // Covariance du bruit de processus (Q)
    kf->Q[0][0] = Q_offset;  // Bruit sur offset (ns²)
    kf->Q[0][1] = 0.0l;
    kf->Q[1][0] = 0.0l;
    kf->Q[1][1] = Q_drift;   // Bruit sur drift (ppb²)

    kf->R = R;               // Bruit de mesure (ns²)
    //kf->dt = 1.0;            // Intervalle PPS (1 seconde)
}

// Prédiction
double kalman_predict(struct kalman_t * kf, double * drift_ppb) {
    // x_pred = F * x
    double x_pred[2];
    x_pred[0] = kf->F[0][0] * kf->x[0] + kf->F[0][1] * kf->x[1];
    x_pred[1] = kf->F[1][0] * kf->x[0] + kf->F[1][1] * kf->x[1];

    // P_pred = F * P * F^T + Q
    double P_pred[2][2] = {{0.0l, 0.0l}, {0.0l, 0.0l}};
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            for (int k = 0; k < 2; k++) {
                P_pred[i][j] += kf->F[i][k] * kf->P[k][j];
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            P_pred[i][j] += kf->Q[i][j];
        }
    }

    // Copie dans kf
    kf->x[0] = x_pred[0];
    kf->x[1] = x_pred[1];
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            kf->P[i][j] = P_pred[i][j];
        }
    }
    if (drift_ppb != NULL) {
        *drift_ppb = x_pred[1];
    }
    return x_pred[0];
}

// Mise à jour (correction)
double kalman_update(struct kalman_t * kf,
                     double offset_ns,
                     double * drift_ppb) {
    // Calcul du gain de Kalman K = P * H^T / (H * P * H^T + R)
    double HPT[2] = {0.0l, 0.0l};
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            HPT[i] += kf->H[j] * kf->P[j][i];
        }
    }
    double denom = kf->H[0] * HPT[0] + kf->H[1] * HPT[1] + kf->R;
    double K[2];
    K[0] = HPT[0] / denom;
    K[1] = HPT[1] / denom;

    // Correction de l'état : x = x_pred + K * (measurement - H * x_pred)
    double y = offset_ns - (kf->H[0] * kf->x[0] + kf->H[1] * kf->x[1]);
    kf->x[0] += K[0] * y;
    kf->x[1] += K[1] * y;

    // Mise à jour de la covariance : P = (I - K * H) * P
    double I_KH[2][2] = {{1.0 - K[0] * kf->H[0], -K[0] * kf->H[1]},
                         {-K[1] * kf->H[0], 1.0 - K[1] * kf->H[1]}};
    double new_P[2][2] = {{0.0l, 0.0l}, {0.0l, 0.0l}};
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            for (int k = 0; k < 2; k++) {
                new_P[i][j] += I_KH[i][k] * kf->P[k][j];
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            kf->P[i][j] = new_P[i][j];
        }
    }
    if (drift_ppb != NULL) {
        *drift_ppb = kf->x[1];
    }
    return kf->x[0];
}

#ifdef KALMAN_MAIN
#include "pps_helper.h"
#include <stdio.h>


static double ts2offset_ns (struct timespec const * ts) {
     double offset_ns = ts->tv_nsec;
     if (offset_ns > 1e9/2) {
     	offset_ns -= 1e9;
     }
     return offset_ns;
}

int main (int argc, char ** argv) {

    struct pps_t * pps = pps_open("/dev/pps0", true);
    if (NULL == pps) {
        return -1;
    }
    struct timespec ts;
    pps_get_timestamp (pps, &ts);

    struct kalman_t kf;
    double R = 500*500; //500ns RMS
    double Q_drift = 100*100; //100ppb RMS
    double Q_offset = 500*500; //100ppb RMS
    	
    kalman_init(&kf,
		ts2offset_ns(&ts), 0.0l,
	       	Q_offset, Q_drift, R);

    while (true) {
        double drift_ppb = 0.0l;
        double offset_ns = kalman_predict(&kf, &drift_ppb);
        printf("PREDICTION: %.0lfns %.0lfppb\n", offset_ns, drift_ppb);
        pps_get_timestamp (pps, &ts);
        printf("MEASURE: %ldns\n", ts.tv_nsec);
        offset_ns = kalman_update(&kf, ts2offset_ns(&ts), &drift_ppb);
        printf("UPDATE: %.0lfns %.0lfppb\n\n", offset_ns, drift_ppb);
    }

    pps_close(pps);
    return 0;
}

#endif // KALMAN_MAIN

