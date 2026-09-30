# Pre-screen rules

Last updated: 2026-09-30

## Purpose

New leads are checked for obvious no-gos **before** any paid research runs.
A lead that matches a rule is set to Skip with the reason "Pre-screen: <reason>".
This saves research and lookup spend on leads we would never contact.

## Where it lives

- A formula field called **Pre-screen** on the Leads table in Airtable. The full formula is in [pre-screen-formula.txt](pre-screen-formula.txt).
- TL3 reads that field. If it has a value, the lead goes to Skip and skips research. If it is empty, the lead continues.
- TL3 holds no copy of the rules. To change the rules, edit the formula field in Airtable.

## Order of checks (first match wins)

The text checked is **Name + Business + the Google Maps category** (from Notes), case-insensitive.
Short words like "md", "f45", "bjj" and "dpt" only match as whole words.

1. Role is Chiropractor or Physical therapist: **Licensed clinician**
2. Role is "Med spa / clinic": **Med spa or clinic**
3. Text matches med-spa or clinic words: **Med spa or clinic**
   med spa, medical spa, botox, injectable, filler, laser hair, IV therapy/drip/lounge/bar/hydration/infusion, drip bar, infusion center/clinic,
   hydration therapy, longevity, hormone, TRT, ketamine, weight loss, semaglutide, body contouring, slimming, peptide, regenerative, regen, anti-aging
4. Text matches clinician words: **Licensed clinician**
   chiropractic, physical therapy, physiotherapy, physio, rehab, acupuncture, sports medicine, orthopedic, podiatry, DPT, medical, clinic, MD
5. Text matches a franchise or chain name: **Franchise or chain**
   F45, Orangetheory, 24 Hour Fitness, LA Fitness, Planet Fitness, Equinox, Crunch, Anytime Fitness, Snap Fitness, Gold's Gym, Burn Boot Camp,
   Alloy, Club Pilates, Pure Barre, YogaSix, CorePower, Solidcore, StretchLab, Row House, CycleBar, Rumble, Title Boxing, 9Round, UFC Gym,
   EoS, Chuze, YMCA, Bay Club, Life Time, Camp Gladiator, HOTWORX, Barry's, SoulCycle, Massage Envy, World Gym, Blink, Xponential,
   The Exercise Coach, YouFit, Curves, and similar
6. Text matches martial-arts words: **Martial-arts academy**
   jiu-jitsu, BJJ, Gracie (Barra, Jiu, Academy and similar), 10th Planet, karate, taekwondo, judo, martial arts, kung fu, krav maga, dojo, hapkido, aikido
7. Text matches kids words: **Kids program**
   kid(s), youth, family, children, tot(s), teen(s)
8. Address has a California ZIP outside 91900 to 92199: **Outside San Diego County**
9. No email and no Instagram handle: **No email or Instagram**

Anything left over has an empty Pre-screen value and moves on to research.

## Back-test (2026-09-28)

Run against past leads, the rules caught 97 of the 172 leads that had been skipped after paid research.
The only good leads it would have flagged wrongly were two that were already past this stage.
All nine BJJ gyms it flagged had kids programs.

## Known limits

- It reads names and Maps categories, not websites. A kids program with a neutral name can still get through; the later research step catches some of these.
- Word lists are short on purpose. Add a term when a repeat offender shows up.
- The ZIP check only works when the address has a "CA 9xxxx" ZIP.
