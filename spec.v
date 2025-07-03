(** Appendix B. Rocq Formal Specifications *)

Require Import Coq.Reals.Reals.
Require Import Coq.micromega.Lra.
Require Import Coq.Reals.Rpower.
Require Import Coq.Reals.Rsqrt.
Open Scope R_scope.

(* -------------------------------------------------------------------- *)
(** B.1. Flash Luminance Threshold *)
Module FlashLuminanceThreshold.

  (** Gamma expansion from sRGB to linear space. *)
  Definition gammaExpand (C : R) : R :=
    if Rle_dec C 0.04045 then
      C / 12.92
    else
      Rpower ((C + 0.055) / 1.055) 2.4.

  (** The three sRGB channels for each frame. *)
  Parameter R_s G_s B_s : nat -> nat -> nat -> R.

  (** Relative luminance I(f, x, y). *)
  Definition I (f x y : nat) : R :=
    0.2126 * gammaExpand (R_s f x y)
  + 0.7152 * gammaExpand (G_s f x y)
  + 0.0722 * gammaExpand (B_s f x y).

  (** Michelson contrast between two luminances. *)
  Definition michelson_contrast (f1 f2 x y : nat) : R :=
    let i1 := I f1 x y in
    let i2 := I f2 x y in
    if Rle_dec (i1 + i2) 0 then 0
    else Rabs (i2 - i1) / (i1 + i2).

  (** A single harmful pixel transition. *)
  Definition harmful_transition (f1 f2 x y : nat) : Prop :=
    let i1 := I f1 x y in
    let i2 := I f2 x y in
    ((i1 > 0.8 /\ i2 > 0.8 /\ michelson_contrast f1 f2 x y >= (1 / 17)) 
     \/ Rabs (i2 - i1) >= 0.1).

  (** A pair of opposing changes in luminance. *)
  Definition opposing_changes (f1 f2 f3 x y : nat) : Prop :=
    let i1 := I f1 x y in
    let i2 := I f2 x y in
    let i3 := I f3 x y in
    (i2 > i1 /\ i3 < i2) \/ (i2 < i1 /\ i3 > i2).

  (** A flash occurs when two consecutive harmful transitions oppose. *)
  Definition is_flash (f1 f2 f3 x y : nat) : Prop :=
    harmful_transition f1 f2 x y
    /\ harmful_transition f2 f3 x y
    /\ opposing_changes f1 f2 f3 x y.

End FlashLuminanceThreshold.

(* -------------------------------------------------------------------- *)
(** B.2. Flash Color Threshold *)
Module FlashColorThreshold.

  (** Gamma expansion (reuse from sRGB). *)
  Definition gammaExpand (C : R) : R :=
    if Rle_dec C 0.04045 then
      C / 12.92
    else
      Rpower ((C + 0.055) / 1.055) 2.4.

  (** sRGB channels again. *)
  Parameter R_s G_s B_s : nat -> nat -> nat -> R.

  (** Linear RGB. *)
  Definition R_lin (f x y : nat) := gammaExpand (R_s f x y).
  Definition G_lin (f x y : nat) := gammaExpand (G_s f x y).
  Definition B_lin (f x y : nat) := gammaExpand (B_s f x y).

  (** Convert to CIE XYZ (D65). *)
  Definition X (f x y : nat) : R :=
    0.4124 * R_lin f x y
  + 0.3576 * G_lin f x y
  + 0.1805 * B_lin f x y.

  Definition Y (f x y : nat) : R :=
    0.2126 * R_lin f x y
  + 0.7152 * G_lin f x y
  + 0.0722 * B_lin f x y.

  Definition Z (f x y : nat) : R :=
    0.0193 * R_lin f x y
  + 0.1192 * G_lin f x y
  + 0.9505 * B_lin f x y.

  (** CIE 1976 UCS chromaticity coordinates. *)
  Definition denom (f x y : nat) : R :=
    X f x y + 15 * Y f x y + 3 * Z f x y.

  Definition u_prime (f x y : nat) : R :=
    let d := denom f x y in
    if Rle_dec d 0 then 0 else (4 * X f x y) / d.

  Definition v_prime (f x y : nat) : R :=
    let d := denom f x y in
    if Rle_dec d 0 then 0 else (9 * Y f x y) / d.

  (** Euclidean color difference in (u′,v′). *)
  Definition color_diff_1976 (f1 f2 x y : nat) : R :=
    let u1 := u_prime f1 x y in
    let v1 := v_prime f1 x y in
    let u2 := u_prime f2 x y in
    let v2 := v_prime f2 x y in
    Rsqrt ((u1 - u2) ^ 2 + (v1 - v2) ^ 2).

  (** Ratio of red in linear RGB. *)
  Definition red_ratio (f x y : nat) : R :=
    let r := R_lin f x y in
    let g := G_lin f x y in
    let b := B_lin f x y in
    let s := r + g + b in
    if Rle_dec s 0 then 0 else r / s.

  (** A single harmful red transition. *)
  Definition harmful_red_transition (f1 f2 x y : nat) : Prop :=
    (red_ratio f1 x y >= 0.8 \/ red_ratio f2 x y >= 0.8)
    /\ color_diff_1976 f1 f2 x y > 0.2.

  (** Opposing changes in red ratio. *)
  Definition opposing_red_changes (f1 f2 f3 x y : nat) : Prop :=
    let r1 := red_ratio f1 x y in
    let r2 := red_ratio f2 x y in
    let r3 := red_ratio f3 x y in
    (r2 > r1 /\ r3 < r2) \/ (r2 < r1 /\ r3 > r2).

  (** A red flash. *)
  Definition is_red_flash (f1 f2 f3 x y : nat) : Prop :=
    harmful_red_transition f1 f2 x y
    /\ harmful_red_transition f2 f3 x y
    /\ opposing_red_changes f1 f2 f3 x y.

End FlashColorThreshold.

(* -------------------------------------------------------------------- *)
(** B.3. Flash Area Threshold *)
Module FlashAreaThreshold.

  (** Screen geometry and resolution. *)
  Parameter Frame : Type.
  Parameter A : Frame -> Frame -> R.
  Parameter w h : R.    (* width and height in pixels *)
  Parameter S : R.      (* screen diagonal in inches *)

  Axiom S_pos : 0 < S.

  Definition theta_h_deg : R := 10.
  Definition theta_v_deg : R := 7.5.

  Definition deg_to_rad (x : R) : R := x * PI / 180.

  (** Pixels per inch (approximate). *)
  Definition ppi : R := sqrt (w^2 + h^2) / S.

  (** Flash area threshold in pixels² given viewing distance d (in inches). *)
  Definition flash_area_threshold (d : R) : R :=
    let θh := deg_to_rad theta_h_deg in
    let θv := deg_to_rad theta_v_deg in
    let area_inch := (d * θh) * (d * θv) in
    area_inch * (ppi ^ 2) * 0.25.

  (** No harmful flash if the flashed area is below threshold. *)
  Definition no_harmful_flash (f1 f2 : Frame) (d : R) : Prop :=
    0 <= d -> A f1 f2 <= flash_area_threshold d.

End FlashAreaThreshold.

(* -------------------------------------------------------------------- *)
(** B.4. Flash Frequency Threshold *)
Module FlashFrequencyThreshold.

  (** Total video duration T (seconds). *)
  Parameter T : R.
  Axiom T_pos : 0 < T.

  (** Number of flashes in [t, t+1). *)
  Parameter F_gen F_red : R -> nat.

  (** Respects the guideline if ≤3 flashes of each kind per second. *)
  Definition respects_flash_rate : Prop :=
    forall t : R,
      0 <= t <= T - 1 ->
        F_gen t <= 3 /\ F_red t <= 3.

  (** Harmful if ∃t with ≥4 flashes of either kind in one second. *)
  Definition harmful_video : Prop :=
    exists t : R,
      0 <= t <= T - 1 /\
      (F_gen t >= 4 \/ F_red t >= 4).

End FlashFrequencyThreshold.