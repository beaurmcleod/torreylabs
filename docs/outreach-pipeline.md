# Torrey Labs outreach pipeline: setup and runbook

Last updated: 2026-09-30

> This repo is public. It holds the map of how the pipeline works, not the keys.
> Never add tokens, cookies, passwords, account logins, database IDs or lead data here.
> See "Not stored here" at the bottom.

## What it does

Finds San Diego County fitness businesses, researches them, writes a short personal cold email for each,
sends one per hour, follows up twice, and stops when someone replies, opts out or bounces.

Three parts:

- **Airtable** is the database (base "Torrey Labs", tables **Leads** and **IG Prospects**).
- **Make.com** runs the automations (scenarios named TL1 to TL5b).
- **Claude Code routines** do the writing and sorting steps. They run on the Claude plan, so there are no API credits.

## Flow at a glance

```
Google Maps pull (TL1) ──┐
                         ├─> Leads (New) ──> TL3: pre-screen, then research ──> Researched
Instagram (TL-IG1/2) ────┘        │                 │
   └─> IG Prospects (New)         │                 └─ fails pre-screen ──> Skip (no money spent)
        └─> "Sort IG" routine ────┘
            (Qualified ones are copied into Leads)

Researched ──> "Message packs" routine ──> Ready (or Skip)
Ready + email ──> TL4 sends ──> Sent ──> Follow-up 1 sent ──> Follow-up 2 sent
Any stage ──> TL5b catches replies: Replied, Not interested or Bounced (sequence stops)
```

## The pieces

### Make scenarios

| Scenario | What it does | When it runs | Status (2026-09-30) |
|---|---|---|---|
| TL1 Source Leads, Google Maps | Pulls fitness businesses in San Diego County (12 search terms), adds new places to Leads as New | On demand (press Run) | Active, about $3.35 per run |
| TL-IG1 IG hashtags to IG Prospects | Scrapes local fitness hashtags, looks up each new account, saves to IG Prospects as New | Daily, 4:00am PT | **Paused** (cost) |
| TL-IG2 Maps leads to IG Prospects | Looks up the Instagram profile of Maps leads that have a handle, saves to IG Prospects as New | Daily, 4:30am PT | Active |
| TL3 Research and pre-screen | New leads that fail the pre-screen go to Skip. The rest get web research plus an Instagram profile lookup, then Researched | Every 15 minutes | Active |
| TL4 Cold email sender | Sends one new email per run plus any follow-ups that are due, from the sender mailbox | Hourly, weekdays 8am to 4pm PT | Active |
| TL5b Reply catcher | Watches the sender inbox and updates the lead | Every 15 minutes | Active |

Older scenarios from earlier experiments are off or manual-only. Leave them alone.
Times are Pacific daylight time. Make schedules follow the team time zone, so they shift by an hour when daylight time ends.

### Claude Code routines

| Routine | What it does | When it runs |
|---|---|---|
| Sort IG Prospects | Sorts New IG prospects into Qualified or Skipped using the targeting rules, then copies Qualified ones into Leads as New (one business = one lead) | Daily, about 5:49am PT |
| TL3 message packs | Writes the email and DM pack for Researched leads (up to 40 per run), then sets them to Ready or Skip | Twice daily, about 7:05am and 1:05pm PT |

Each routine passes its full brief to a background agent and reports one line.
The routines are attached to one Claude Code web session. If that session is archived or deleted, the routines may stop and need to be recreated.

## Lead lifecycle (Status in Leads)

`New` -> `Researched` -> `Ready` -> `Sent` -> `Follow-up 1 sent` -> `Follow-up 2 sent`

End states: `Replied`, `Not interested`, `Bounced`, `Skip`, `Error`, `Research failed`.

Email timing:

- Email 1 goes out as a new message.
- Email 2 goes about 3 days later in the same Gmail thread.
- Email 3 goes about 4 days after that (day 7), same thread.
- A reply, a plain "no" or "stop", or a bounce stops the sequence.

How TL4 picks the next lead: highest **Fit score** among leads that are Ready, channel Email, track not Skip,
have a subject and Email 1, have no Gmail thread yet, and whose Email 1 starts with "Hey " (a guard that keeps old-style copy from going out).
Every email gets a footer with an opt-out line and the business postal address (set in TL4's first module).

How TL5b classifies mail: bounce notices mark the lead Bounced. A reply that is only "no", "stop", "unsubscribe" or similar marks it Not interested.
Any other reply marks it Replied and saves the reply text.

## Who we target

Wanted: owner-run fitness businesses in San Diego County with adult clients: trainers and training studios,
strength and conditioning, CrossFit, HYROX, functional and lifting gyms, adult boxing, small-group and boutique studios,
recovery, sauna and cold-plunge studios, nutrition coaches, and local fitness creators who coach people.

Never wanted: licensed clinicians, med spas and similar clinics, kids or youth or family programs,
martial-arts academies (most teach kids), franchises and chains, elite drug-tested athletes and the coaches who prep them,
and anyone selling supplements.

The pre-screen filter enforces most of this automatically before any paid research happens.
Details and the exact formula: [pre-screen-rules.md](pre-screen-rules.md) and [pre-screen-formula.txt](pre-screen-formula.txt).

Message copy rules (tone, what may and may not be claimed, offer terms) live in the "message packs" routine brief, not in this repo.

## Accounts and access (nothing secret here)

| System | How it's connected |
|---|---|
| Make | One team. Each app connection is stored inside Make. |
| Airtable | Token stored in the Make connection and in app settings only. Never paste it into chats, code or fields. |
| Email | The Torrey Labs sender mailbox, connected in Make. Do not use any other business or personal mailbox for this pipeline. |
| Research | Perplexity API key stored in the Make connection (model sonar-pro). |
| Apify | New account connected in Make as "Apify NEW". Expected to be on the free plan; confirm the plan and credit in Apify billing. The old paid plan was cancelled on 2026-09-30. |
| Claude Code routines | Managed in the Claude Code web app. |

## Costs and limits

| Item | Cost |
|---|---|
| Google Maps pull (TL1) | about $3.35 per run |
| Instagram hashtag scrape (TL-IG1) | about $0.74 per daily run (about $22 a month) |
| Instagram profile lookup | about $0.0023 per profile |
| Web research per lead (Perplexity) | roughly 2 cents, only for leads that pass the pre-screen |
| Sending email | No per-email cost |

On Apify's free plan, $5 of usage is included each month, no card is needed, and it stops instead of charging when the credit runs out.
That covers roughly one Maps pull plus some profile lookups each month. Check which plan the connected account is actually on.

When credit runs out, Apify-based runs fail with "Monthly usage hard limit exceeded".
Make turns a scenario off after 3 failed runs in a row, so turn the flows back on once credit is available.
Sending (TL4) and reply catching (TL5b) do not use Apify, so they keep working.

## Daily health check

1. Make: TL4 should show one successful run per hour on weekdays, 8am to 4pm PT.
2. Airtable Leads: count of Sent today, and look for any Error, Bounced or Replied.
3. Make: no red runs on TL3, TL5b or TL-IG2.
4. Apify: usage against the $5 credit.
5. Sender inbox: read replies yourself. TL5b only labels them.

## Notes for Claude sessions

- Editing a Make scenario through the API replaces the whole blueprint. Fetch the current one first, change only what's needed, and check the schedule afterward.
- To move a flow to another Apify account, create the new Make connection first (a Make credential request link works and keeps secrets out of chat), then update every Apify module in TL1, TL3, TL-IG1 and TL-IG2.
- Make's execution detail via the API returns only a status, not module outputs. To see results, write them to a temporary data store, read it, then delete the store.
- Gmail thread IDs encode the send time: `int(hex_id, 16) >> 20` is milliseconds since the epoch. Useful for auditing real send times.
- Ask the owner before anything outward-facing: sending or scheduling emails or messages, changing send times, or turning sending on or off.

## Change log

- 2026-09-28: Added the pre-screen formula column in Airtable and a matching skip route in TL3. Rebuilt TL3 from the approved no-API-credits version. Removed BJJ and MMA searches from TL1 (mostly kids programs). Aligned the Sort IG routine with the message-pack targeting rules.
- 2026-09-29: Apify monthly cap reached. Paused the hashtag finder (TL-IG1).
- 2026-09-30: Moved TL1, TL3, TL-IG1 and TL-IG2 (and one unrelated daily flow) to a new Apify account. Tested with one profile lookup. Old paid plan cancelled.
- Earlier in September: email style set to short and personable, certificate links removed from emails, kids programs skipped, email sending turned on (TL4 and TL5b).

## Open items

- Instagram DM sending is off. It needs a safer approach before anything is turned back on.
- The hashtag finder stays paused until there is a monthly budget for it.
- Decide whether to move to a paid Apify plan once the free credit is not enough.
- Apify is retiring flat-fee actor rentals on 2026-10-01. Confirm no rental charge remains on the old account.
- Consider making this repo private if more detail is ever added.

## Not stored here (on purpose)

This repo is public, so these are kept out: API tokens and session cookies, account logins and email addresses,
Airtable base and field IDs, Make scenario and connection IDs, the full routine briefs (copy rules and offer terms),
and all lead data. They live in Make, Airtable and the Claude Code routines. Keep a private copy of the routine briefs somewhere private
so the routines can be rebuilt if needed.
