/* basé sur on smallpt, a Path Tracer by Kevin Beason, 2008
 *  	http://www.kevinbeason.com/smallpt/ 
 *
 * Converti en C et modifié par Charles Bouillaguet, 2019
 *
 * Pour des détails sur le processus de rendu, lire :
 * 	https://docs.google.com/open?id=0B8g97JkuSSBwUENiWTJXeGtTOHFmSm51UC01YWtCZw
 */

#define _XOPEN_SOURCE
#include <math.h>   
#include <stdlib.h> 
#include <stdio.h>
#include <stdbool.h>
#include <mpi.h>
#include <sys/time.h>
#include <sys/stat.h>  /* pour mkdir    */ 
#include <unistd.h>    /* pour getuid   */
#include <sys/types.h> /* pour getpwuid */
#include <pwd.h>       /* pour getpwuid */
#include <time.h>

enum Refl_t {DIFF, SPEC, REFR};   /* types de matériaux (DIFFuse, SPECular, REFRactive) */

struct Sphere { 
	double radius; 
	double position[3];
	double emission[3];     /* couleur émise (=source de lumière) */
	double color[3];        /* couleur de l'objet RGB (diffusion, refraction, ...) */
	enum Refl_t refl;       /* type de reflection */
	double max_reflexivity;
};

double my_gettimeofday(){
  struct timeval tmp_time;
  gettimeofday(&tmp_time, NULL);
  return tmp_time.tv_sec + (tmp_time.tv_usec * 1.0e-6L);
}

static const int KILL_DEPTH = 7;
static const int SPLIT_DEPTH = 4;

/* la scène est composée uniquement de spheres */
struct Sphere spheres[] = { 
// radius position,                         emission,     color,              material 
   {1e5,  { 1e5+1,  40.8,       81.6},      {},           {.75,  .25,  .25},  DIFF, -1}, // Left 
   {1e5,  {-1e5+99, 40.8,       81.6},      {},           {.25,  .25,  .75},  DIFF, -1}, // Right 
   {1e5,  {50,      40.8,       1e5},       {},           {.75,  .75,  .75},  DIFF, -1}, // Back 
   {1e5,  {50,      40.8,      -1e5 + 170}, {},           {},                 DIFF, -1}, // Front 
   {1e5,  {50,      1e5,        81.6},      {},           {0.75, .75,  .75},  DIFF, -1}, // Bottom 
   {1e5,  {50,     -1e5 + 81.6, 81.6},      {},           {0.75, .75,  .75},  DIFF, -1}, // Top 
   {16.5, {40,      16.5,       47},        {},           {.999, .999, .999}, SPEC, -1}, // Mirror 
   {16.5, {73,      46.5,       88},        {},           {.999, .999, .999}, REFR, -1}, // Glass 
   {10,   {15,      45,         112},       {},           {.999, .999, .999}, DIFF, -1}, // white ball
   {15,   {16,      16,         130},       {},           {.999, .999, 0},    REFR, -1}, // big yellow glass
   {7.5,  {40,      8,          120},        {},           {.999, .999, 0   }, REFR, -1}, // small yellow glass middle
   {8.5,  {60,      9,          110},        {},           {.999, .999, 0   }, REFR, -1}, // small yellow glass right
   {10,   {80,      12,         92},        {},           {0, .999, 0},       DIFF, -1}, // green ball
   {600,  {50,      681.33,     81.6},      {12, 12, 12}, {},                 DIFF, -1},  // Light 
   {5,    {50,      75,         81.6},      {},           {0, .682, .999}, DIFF, -1}, // occlusion, mirror
}; 


/********** micro BLAS LEVEL-1 + quelques fonctions non-standard **************/
static inline void copy(const double *x, double *y)
{
	for (int i = 0; i < 3; i++)
		y[i] = x[i];
} 

static inline void zero(double *x)
{
	for (int i = 0; i < 3; i++)
		x[i] = 0;
} 

static inline void axpy(double alpha, const double *x, double *y)//a*x+y
{
	for (int i = 0; i < 3; i++)
		y[i] += alpha * x[i];
} 

static inline void scal(double alpha, double *x)// multiplie par un scalaire
{
	for (int i = 0; i < 3; i++)
		x[i] *= alpha;
} 

static inline double dot(const double *a, const double *b)//Produit scalaire
{ 
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
} 

static inline double nrm2(const double *a)
{
	return sqrt(dot(a, a));
}

/********* fonction non-standard *************/
static inline void mul(const double *x, const double *y, double *z)
{
	for (int i = 0; i < 3; i++)
		z[i] = x[i] * y[i];
} 

static inline void normalize(double *x)
{
	scal(1 / nrm2(x), x);
}

/* produit vectoriel */
static inline void cross(const double *a, const double *b, double *c)
{
	c[0] = a[1] * b[2] - a[2] * b[1];
	c[1] = a[2] * b[0] - a[0] * b[2];
	c[2] = a[0] * b[1] - a[1] * b[0];
}

/****** tronque *************/
static inline void clamp(double *x) 
{
	for (int i = 0; i < 3; i++) {
		if (x[i] < 0)
			x[i] = 0;
		if (x[i] > 1)
			x[i] = 1;
	}
} 

/******************************* calcul des intersections rayon / sphere *************************************/
   
// returns distance, 0 if nohit 
double sphere_intersect(const struct Sphere *s, const double *ray_origin, const double *ray_direction)
{ 
	double op[3];
	// Solve t^2*d.d + 2*t*(o-p).d + (o-p).(o-p)-R^2 = 0 
	copy(s->position, op);
	axpy(-1, ray_origin, op);
	double eps = 1e-4;
	double b = dot(op, ray_direction);
	double discriminant = b * b - dot(op, op) + s->radius * s->radius; 
	if (discriminant < 0)
		return 0;   /* pas d'intersection */
	else 
		discriminant = sqrt(discriminant);
	/* détermine la plus petite solution positive (i.e. point d'intersection le plus proche, mais devant nous) */
	double t = b - discriminant;
	if (t > eps) {
		return t;
	} else {
		t = b + discriminant;
		if (t > eps)
			return t;
		else
			return 0;  /* cas bizarre, racine double, etc. */
	}
}

/* détermine si le rayon intersecte l'une des spere; si oui renvoie true et fixe t, id */
bool intersect(const double *ray_origin, const double *ray_direction, double *t, int *id)
{ 
	int n = sizeof(spheres) / sizeof(struct Sphere);
	double inf = 1e20; 
	*t = inf;
	for (int i = 0; i < n; i++) {
		double d = sphere_intersect(&spheres[i], ray_origin, ray_direction);
		if ((d > 0) && (d < *t)) {
			*t = d;
			*id = i;
		} 
	}
	return *t < inf;
} 

/* calcule (dans out) la lumiance reçue par la camera sur le rayon donné */
void radiance(const double *ray_origin, const double *ray_direction, int depth, unsigned short *PRNG_state, double *out)
{ 
	int id = 0;                             // id de la sphère intersectée par le rayon
	double t;                               // distance à l'intersection
	if (!intersect(ray_origin, ray_direction, &t, &id)) {
		zero(out);    // if miss, return black 
		return; 
	}
	const struct Sphere *obj = &spheres[id];
	
	/* point d'intersection du rayon et de la sphère */
	double x[3];
	copy(ray_origin, x);
	axpy(t, ray_direction, x);
	
	/* vecteur normal à la sphere, au point d'intersection */
	double n[3];  
	copy(x, n);
	axpy(-1, obj->position, n);
	normalize(n);
	
	/* vecteur normal, orienté dans le sens opposé au rayon 
	   (vers l'extérieur si le rayon entre, vers l'intérieur s'il sort) */
	double nl[3];
	copy(n, nl);
	if (dot(n, ray_direction) > 0)
		scal(-1, nl);
	
	/* couleur de la sphere */
	double f[3];
	copy(obj->color, f);
	double p = obj->max_reflexivity;

	/* processus aléatoire : au-delà d'une certaine profondeur,
	   décide aléatoirement d'arrêter la récusion. Plus l'objet est
	   clair, plus le processus a de chance de continuer. */
	depth++;
	if (depth > KILL_DEPTH) {
		if (erand48(PRNG_state) < p) {
			scal(1 / p, f); 
		} else {
			copy(obj->emission, out);
			return;
		}
	}

	/* Cas de la réflection DIFFuse (= non-brillante). 
	   On récupère la luminance en provenance de l'ensemble de l'univers. 
	   Pour cela : (processus de monte-carlo) on choisit une direction
	   aléatoire dans un certain cone, et on récupère la luminance en 
	   provenance de cette direction. */
	if (obj->refl == DIFF) {
		double r1 = 2 * M_PI * erand48(PRNG_state);  /* angle aléatoire */
		double r2 = erand48(PRNG_state);             /* distance au centre aléatoire */
		double r2s = sqrt(r2); 
		
		double w[3];   /* vecteur normal */
		copy(nl, w);
		
		double u[3];   /* u est orthogonal à w */
		double uw[3] = {0, 0, 0};
		if (fabs(w[0]) > .1)
			uw[1] = 1;
		else
			uw[0] = 1;
		cross(uw, w, u);
		normalize(u);
		
		double v[3];   /* v est orthogonal à u et w */
		cross(w, u, v);
		
		double d[3];   /* d est le vecteur incident aléatoire, selon la bonne distribution */
		zero(d);
		axpy(cos(r1) * r2s, u, d);
		axpy(sin(r1) * r2s, v, d);
		axpy(sqrt(1 - r2), w, d);
		normalize(d);
		
		/* calcule récursivement la luminance du rayon incident */
		double rec[3];
		radiance(x, d, depth, PRNG_state, rec);
		
		/* pondère par la couleur de la sphère, prend en compte l'emissivité */
		mul(f, rec, out);
		axpy(1, obj->emission, out);
		return;
	}

	/* dans les deux autres cas (réflection parfaite / refraction), on considère le rayon
	   réfléchi par la spère */

	double reflected_dir[3];
	copy(ray_direction, reflected_dir);
	axpy(-2 * dot(n, ray_direction), n, reflected_dir);

	/* cas de la reflection SPEculaire parfaire (==mirroir) */
	if (obj->refl == SPEC) { 
		double rec[3];
		/* calcule récursivement la luminance du rayon réflechi */
		radiance(x, reflected_dir, depth, PRNG_state, rec);
		/* pondère par la couleur de la sphère, prend en compte l'emissivité */
		mul(f, rec, out);
		axpy(1, obj->emission, out);
		return;
	}

	/* cas des surfaces diélectriques (==verre). Combinaison de réflection et de réfraction. */
	bool into = dot(n, nl) > 0;      /* vient-il de l'extérieur ? */
	double nc = 1;                   /* indice de réfraction de l'air */
	double nt = 1.5;                 /* indice de réfraction du verre */
	double nnt = into ? (nc / nt) : (nt / nc);
	double ddn = dot(ray_direction, nl);
	
	/* si le rayon essaye de sortir de l'objet en verre avec un angle incident trop faible,
	   il rebondit entièrement */
	double cos2t = 1 - nnt * nnt * (1 - ddn * ddn);
	if (cos2t < 0) {
		double rec[3];
		/* calcule seulement le rayon réfléchi */
		radiance(x, reflected_dir, depth, PRNG_state, rec);
		mul(f, rec, out);
		axpy(1, obj->emission, out);
		return;
	}
	
	/* calcule la direction du rayon réfracté */
	double tdir[3];
	zero(tdir);
	axpy(nnt, ray_direction, tdir);
	axpy(-(into ? 1 : -1) * (ddn * nnt + sqrt(cos2t)), n, tdir);

	/* calcul de la réflectance (==fraction de la lumière réfléchie) */
	double a = nt - nc;
	double b = nt + nc;
	double R0 = a * a / (b * b);
	double c = 1 - (into ? -ddn : dot(tdir, n));
	double Re = R0 + (1 - R0) * c * c * c * c * c;   /* réflectance */
	double Tr = 1 - Re;                              /* transmittance */
	
	/* au-dela d'une certaine profondeur, on choisit aléatoirement si
	   on calcule le rayon réfléchi ou bien le rayon réfracté. En dessous du
	   seuil, on calcule les deux. */
	double rec[3];
	if (depth > SPLIT_DEPTH) {
		double P = .25 + .5 * Re;             /* probabilité de réflection */
		if (erand48(PRNG_state) < P) {
			radiance(x, reflected_dir, depth, PRNG_state, rec);
			double RP = Re / P;
			scal(RP, rec);
		} else {
			radiance(x, tdir, depth, PRNG_state, rec);
			double TP = Tr / (1 - P); 
			scal(TP, rec);
		}
	} else {
		double rec_re[3], rec_tr[3];
		radiance(x, reflected_dir, depth, PRNG_state, rec_re);
		radiance(x, tdir, depth, PRNG_state, rec_tr);
		zero(rec);
		axpy(Re, rec_re, rec);
		axpy(Tr, rec_tr, rec);
	}
	/* pondère, prend en compte la luminance */
	mul(f, rec, out);
	axpy(1, obj->emission, out);
	return;
}

double wtime()
{
	struct timeval ts;
	gettimeofday(&ts, NULL);
	return (double)ts.tv_sec + ts.tv_usec / 1E6;
}

int toInt(double x)
{
	return pow(x, 1 / 2.2) * 255 + .5;   /* gamma correction = 2.2 */
} 

int main(int argc, char **argv)
{ 
	/* Petit cas test (small, quick and dirty): */
	int w = 320;
	int h = 200;
	int samples = 200;

	/* Gros cas test (big, slow and pretty): */
	/* int w = 3840; */
	/* int h = 2160; */
	/* int samples = 5000;  */

	if (argc == 2) 
		samples = atoi(argv[1]) / 4;

	static const double CST = 0.5135;  /* ceci défini l'angle de vue */
	double camera_position[3] = {50, 52, 295.6};
	double camera_direction[3] = {0, -0.042612, -1};
	normalize(camera_direction);

	/* incréments pour passer d'un pixel à l'autre */
	double cx[3] = {w * CST / h, 0, 0};    
	double cy[3];
	cross(cx, camera_direction, cy);  /* cross: produit vectoriel. cy est orthogonal à cx ET à la direction dans laquelle regarde la caméra */
	normalize(cy);
	scal(CST, cy);

	/* précalcule la norme infinie des couleurs */
	int n = sizeof(spheres) / sizeof(struct Sphere); //Nombre de sphères dans le tableau sphère
	for (int i = 0; i < n; i++) {//La valeure la plus élevé parmis les 3 composantes RGB devient la valeure de la reflexivité
		double *f = spheres[i].color;
		if ((f[0] > f[1]) && (f[0] > f[2]))
			spheres[i].max_reflexivity = f[0]; 
		else {
			if (f[1] > f[2])
				spheres[i].max_reflexivity = f[1];
			else
				spheres[i].max_reflexivity = f[2]; 
		}
	}

	 /* debut du chronometrage */
  	double debut = my_gettimeofday();

	int rang, size, tag=10;
  	MPI_Init(&argc, &argv);
  	MPI_Comm_size(MPI_COMM_WORLD, &size);
  	MPI_Comm_rank(MPI_COMM_WORLD, &rang);
  	MPI_Status status;


  	//double *image = malloc(3 * w * h * sizeof(*image));
  	if (rang==0)

  	{
  		int count;
  		int num_process;
  		//double *image = malloc(3 * w * h * sizeof(*image));
		double *image = malloc(3 * w * h * sizeof(double));
		if (image == NULL) {
			perror("Impossible d'allouer l'image");
			exit(1);
		}
		unsigned int *ouvrier_tache=(unsigned int*)malloc(size*sizeof(unsigned int));
   		for (int i = 0; i < size; ++i)
   		{
   			ouvrier_tache[i]=i-1;
		}

		int nbr_process_fini=0;
		int affected= size-1;

		while(nbr_process_fini<size-1){
			MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
      		MPI_Get_count(&status, MPI_UNSIGNED_CHAR, &count);
      		num_process= status.MPI_SOURCE;
			MPI_Recv(image+ 3*(h-1 - ouvrier_tache[num_process])*w, count , MPI_DOUBLE, num_process, status.MPI_TAG, MPI_COMM_WORLD, &status);
			if (affected==h) //Toutes les lignes ont été faite
     		{
        		int temp=0;
        		MPI_Send(&temp, 1, MPI_UNSIGNED, num_process, 0/*tag*/, MPI_COMM_WORLD); 
        		nbr_process_fini++; 
     		}else{ 
        		MPI_Send(&affected, 1, MPI_UNSIGNED, num_process, 10/*tag*/, MPI_COMM_WORLD); 
        		ouvrier_tache[num_process]=affected;
       			affected++;
       		}

       		//printf("affected=%d", affected);
		}
		//printf("image[7]=%f", image[100]);


		//printf("rang=%d\n",rang );
		struct passwd *pass; 
		char nom_sortie[100] = "";
		char nom_rep[30] = "";
		//printf("image[7]=%f", image[f]);
		pass = getpwuid(getuid()); 
		//sprintf(nom_rep, "/tmp/%s", pass->pw_name);
		sprintf(nom_rep, "%s", pass->pw_name);
		printf("nom répertoir %s\n", nom_rep);
		if(mkdir(nom_rep, S_IRWXU)==0)
			printf("Problème création dossier %s\n",nom_rep );
		sprintf(nom_sortie, "%s/image_test.ppm", nom_rep);
		
		FILE *f = fopen(nom_sortie, "w");
		if(f==NULL)
			printf("Problème création %s\n",nom_sortie );
		fprintf(f, "P3\n%d %d\n%d\n", w, h, 255); 
		for (int i = 0; i < w * h; i++) 
	  		fprintf(f,"%d %d %d ", toInt(image[3 * i]), toInt(image[3 * i + 1]), toInt(image[3 * i + 2])); 
		fclose(f); 
		free(image);

		double fin = my_gettimeofday();
		fprintf( stderr, " Temps total de calcul : %g sec\n",
     	fin - debut);

  	}


  	if(rang>0){
  	int ligne=rang-1;
  	bool continu=true;
  	double *img = malloc(3 * w *  sizeof(*img));
	if (img == NULL) {
		perror("Impossible d'allouer img");
		exit(1);
	}

	/* boucle principale */
	//for (int i = 0; i < h; i++) {
 		unsigned short PRNG_state[3] = {0, 0, ligne*ligne*ligne};
		for (unsigned short j = 0; j < w; j++) {
			/* calcule la luminance d'un pixel, avec sur-échantillonnage 2x2 */
			double pixel_radiance[3] = {0, 0, 0};
			for (int sub_i = 0; sub_i < 2; sub_i++) {
				for (int sub_j = 0; sub_j < 2; sub_j++) {
					double subpixel_radiance[3] = {0, 0, 0};
					/* simulation de monte-carlo : on effectue plein de lancers de rayons et on moyenne */
					for (int s = 0; s < samples; s++) { 
						/* tire un rayon aléatoire dans une zone de la caméra qui correspond à peu près au pixel à calculer */
						double r1 = 2 * erand48(PRNG_state);
						double dx = (r1 < 1) ? sqrt(r1) - 1 : 1 - sqrt(2 - r1); 
						double r2 = 2 * erand48(PRNG_state);
						double dy = (r2 < 1) ? sqrt(r2) - 1 : 1 - sqrt(2 - r2);
						double ray_direction[3];
						copy(camera_direction, ray_direction);
						axpy(((sub_i + .5 + dy) / 2 + ligne) / h - .5, cy, ray_direction);
						axpy(((sub_j + .5 + dx) / 2 + j) / w - .5, cx, ray_direction);
						normalize(ray_direction);

						double ray_origin[3];
						copy(camera_position, ray_origin);
						axpy(140, ray_direction, ray_origin);
						
						/* estime la lumiance qui arrive sur la caméra par ce rayon */
						double sample_radiance[3];
						radiance(ray_origin, ray_direction, 0, PRNG_state, sample_radiance); //void radiance(const double *ray_origin, const double *ray_direction, int depth, unsigned short *PRNG_state, double *out)

						/* fait la moyenne sur tous les rayons */
						axpy(1. / samples, sample_radiance, subpixel_radiance);
					}
					clamp(subpixel_radiance); //S'assure que les coef de subpixel_radiance soient compris entre 0 et 1
					/* fait la moyenne sur les 4 sous-pixels */
					axpy(0.25, subpixel_radiance, pixel_radiance);
				}
			}
			copy(pixel_radiance, img + 3 * j); // <-- retournement vertical
			//copy(pixel_radiance, image + 3 * ((i) * w + j)); // <-- retournement vertical
		}

		MPI_Send(img, w*3, MPI_DOUBLE, 0, tag, MPI_COMM_WORLD);

		while(continu){
			MPI_Recv(&ligne, 1, MPI_UNSIGNED, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
      		//printf("continu= %d\n", continu);
      	//	printf("process %d", rang);
     		if (status.MPI_TAG==0)
      		{
        		continu=false;
      		}else{
        		unsigned short PRNG_state[3] = {0, 0, ligne*ligne*ligne};
				for (unsigned short j = 0; j < w; j++) {
				/* calcule la luminance d'un pixel, avec sur-échantillonnage 2x2 */
					double pixel_radiance[3] = {0, 0, 0};
					for (int sub_i = 0; sub_i < 2; sub_i++) {
						for (int sub_j = 0; sub_j < 2; sub_j++) {
							double subpixel_radiance[3] = {0, 0, 0};
							/* simulation de monte-carlo : on effectue plein de lancers de rayons et on moyenne */
							for (int s = 0; s < samples; s++) { 
								/* tire un rayon aléatoire dans une zone de la caméra qui correspond à peu près au pixel à calculer */
								double r1 = 2 * erand48(PRNG_state);
								double dx = (r1 < 1) ? sqrt(r1) - 1 : 1 - sqrt(2 - r1); 
								double r2 = 2 * erand48(PRNG_state);
								double dy = (r2 < 1) ? sqrt(r2) - 1 : 1 - sqrt(2 - r2);
								double ray_direction[3];
								copy(camera_direction, ray_direction);
								axpy(((sub_i + .5 + dy) / 2 + ligne) / h - .5, cy, ray_direction);
								axpy(((sub_j + .5 + dx) / 2 + j) / w - .5, cx, ray_direction);
								normalize(ray_direction);

								double ray_origin[3];
								copy(camera_position, ray_origin);
								axpy(140, ray_direction, ray_origin);
						
								/* estime la lumiance qui arrive sur la caméra par ce rayon */
								double sample_radiance[3];
								radiance(ray_origin, ray_direction, 0, PRNG_state, sample_radiance); //void radiance(const double *ray_origin, const double *ray_direction, int depth, unsigned short *PRNG_state, double *out)

								/* fait la moyenne sur tous les rayons */
								axpy(1. / samples, sample_radiance, subpixel_radiance);
							}
							clamp(subpixel_radiance); //S'assure que les coef de subpixel_radiance soient compris entre 0 et 1
							/* fait la moyenne sur les 4 sous-pixels */
							axpy(0.25, subpixel_radiance, pixel_radiance);
						}
					}
					copy(pixel_radiance, img + 3 * j); // <-- retournement vertical
					//copy(pixel_radiance, image + 3 * ((i) * w + j)); // <-- retournement vertical
				}

				MPI_Send(img, w*3, MPI_DOUBLE, 0, tag, MPI_COMM_WORLD);

   	   		}
		}
		free(img);
	}




	fprintf(stderr, "\n");

	/* stocke l'image dans un fichier au format NetPbm */
	/*if(rang==0){
		
		printf("rang=%d\n",rang );
		struct passwd *pass; 
		char nom_sortie[100] = "";
		char nom_rep[30] = "";
		//printf("image[7]=%f", image[f]);
		pass = getpwuid(getuid()); 
		sprintf(nom_rep, "/tmp/%s", pass->pw_name);
		printf("nom répertoir %s\n", nom_rep);
		if(mkdir(nom_rep, S_IRWXU)==0)
			printf("Problème création dossier %s\n",nom_rep );
		sprintf(nom_sortie, "%s/image_test.ppm", nom_rep);
		
		FILE *f = fopen(nom_sortie, "w");
		if(f==NULL)
			printf("Problème création %s\n",nom_sortie );
		fprintf(f, "P3\n%d %d\n%d\n", w, h, 255); 
		for (int i = 0; i < w * h; i++) 
	  		fprintf(f,"%d %d %d ", toInt(image[3 * i]), toInt(image[3 * i + 1]), toInt(image[3 * i + 2])); 
		fclose(f); 
		free(image);
	

}*/
	MPI_Finalize();
	//return 0;	
}