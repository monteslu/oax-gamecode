/*
===========================================================================
bg_oax_vehicle_syscalls.h: C wrappers of the vehicle and terrain traps
(1260-1269) for DLL builds; QVM builds use the equ lines in g_syscalls.asm
and cg_syscalls.asm. Included after bg_oax_phys_syscalls.h.
===========================================================================
*/
int trap_Phys_VehicleCreate( int world, const oaxPhysVehicleDef_t *def ) { return syscall( PHYS_VEHICLE_CREATE, world, def ); }
void trap_Phys_VehicleDestroy( int vehicle ) { syscall( PHYS_VEHICLE_DESTROY, vehicle ); }
void trap_Phys_VehicleSetInput( int vehicle, const oaxPhysVehicleInput_t *in ) { syscall( PHYS_VEHICLE_SET_INPUT, vehicle, in ); }
int trap_Phys_VehicleGetState( int vehicle, oaxPhysVehicleState_t *out ) { return syscall( PHYS_VEHICLE_GET_STATE, vehicle, out ); }
int trap_Phys_VehicleSetState( int vehicle, const oaxPhysVehicleState_t *in, int flags ) { return syscall( PHYS_VEHICLE_SET_STATE, vehicle, in, flags ); }
int trap_Phys_WorldAddTerrain( int world, const oaxPhysShapeDef_t *material, int *bodies, int max ) {
	return syscall( PHYS_WORLD_ADD_TERRAIN, world, material, bodies, max );
}
