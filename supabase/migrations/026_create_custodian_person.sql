-- Create the Custodian person for the commonplace book
INSERT INTO persons (id, name, slug, kind, public_domain, bio)
VALUES (
  'c0000000-0000-0000-0000-000000000001',
  'Custodian',
  'custodian',
  'faculty',
  true,
  'The Custodian of the Inquiry Institute maintains the living library, curating knowledge and fostering dialogue across disciplines. Through daily observations and collected links, the Custodian weaves together threads of inquiry from across the digital landscape.'
)
ON CONFLICT (id) DO UPDATE SET
  name = EXCLUDED.name,
  slug = EXCLUDED.slug,
  kind = EXCLUDED.kind,
  public_domain = EXCLUDED.public_domain,
  bio = EXCLUDED.bio;
