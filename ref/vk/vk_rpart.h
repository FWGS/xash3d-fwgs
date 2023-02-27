#pragma once

typedef struct particle_s particle_t;

void CL_DrawParticles( double frametime, particle_t *particles, float partsize );
void CL_DrawTracers( double frametime, particle_t *tracers );
