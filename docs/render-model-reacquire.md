# RenderModel reacquisition

The physical HIL on 2026-10-24 observed valid Direct poses after a lost tracker
recovered, while the model property stayed empty. The offline reproduction loads
the ordinary driver DLL in the strict SDK Mock Host and fails at `recovery did
not restore model` with the unmodified driver.

Two paths caused the failure. `TrackerDevice::Update` erased `renderModel_` on
any presentation ineligibility, including temporary loss or a 500 ms expiry.
The model was set only by `Activate`, so valid tracking could not restore it.
`DeviceProvider` also constructed an unassigned object when the first admitted
pose was temporarily invalid, permanently losing an otherwise valid selection.

`Presentation::evaluate` now distinguishes unassigned, temporarily hidden,
visible, and conflicting identity/config. The immutable model choice survives
temporary invalidity. `TrackerDevice` separately tracks desired visibility,
successful property application, and permanent assignment revocation. Successful
edges write the model name or an empty string once. Failed property writes remain
pending and retry without changing pose validity. Deactivation resets applied
state; activation reapplies the current desired state to the property container.
The Provider initializes desired state before `TrackedDeviceAdded`, supporting
both synchronous and delayed activation.

Source/device/input session, publisher/session/clock, logical tracker, and map
revision still have to match the reviewed immutable selection. An observed
conflict remains revoked even if an old matching tuple returns. Reconnect means
the same approved tuple; a changed publisher/input session requires a new explicit
presentation snapshot. No World, Approval, Calibration, Revision, PICO, pose,
network, registration, asset geometry, texture, or rotation contract changed.

The strict Host runs three cycles each of lost, expiry, invalid pose, rotation
only, and absence/reconnect; verifies changing poses and stable device identity;
counts property writes; injects property failures; and checks deactivate/reactivate,
synchronous activation, first-pose invalidity, first-pose expiry, and fixture
restart. Existing negative assignment/resource tests and generic driver tests
remain active. Asset validation checks exact OBJ/MTL/PNG bytes and lookup closure.
The Host observes property state, not pixels in SteamVR. Actual rendering and
automatic model return still require HIL with the new package.

90-degree pitch remains NOT CHANGED / NOT VERIFIED. The existing model is an
approximation and its physical local-axis alignment is not established.
