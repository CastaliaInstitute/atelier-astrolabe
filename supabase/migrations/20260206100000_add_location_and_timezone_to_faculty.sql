-- ============================================================================
-- Add location (city, region, country) and timezone to faculty
-- Each persona is assigned the location most associated with their life/work
-- ============================================================================

-- Add columns
ALTER TABLE public.faculty ADD COLUMN IF NOT EXISTS location_city text;
ALTER TABLE public.faculty ADD COLUMN IF NOT EXISTS location_region text;
ALTER TABLE public.faculty ADD COLUMN IF NOT EXISTS location_country text;
ALTER TABLE public.faculty ADD COLUMN IF NOT EXISTS timezone text;

-- Create index on timezone for grouping queries
CREATE INDEX IF NOT EXISTS idx_faculty_timezone ON public.faculty(timezone);
CREATE INDEX IF NOT EXISTS idx_faculty_location_country ON public.faculty(location_country);

COMMENT ON COLUMN public.faculty.location_city IS 'City where the persona primarily lived/worked';
COMMENT ON COLUMN public.faculty.location_region IS 'State, province, or region';
COMMENT ON COLUMN public.faculty.location_country IS 'Country (modern name)';
COMMENT ON COLUMN public.faculty.timezone IS 'IANA timezone identifier (e.g. America/New_York)';

-- ============================================================================
-- BOARD OF DIRECTORS / ORIGINAL SEATED FACULTY
-- ============================================================================

-- Ada Lovelace - London, England
UPDATE public.faculty SET
  location_city = 'London', location_region = 'England', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.adalovelace';

-- Al-Khwarizmi - Baghdad (Abbasid capital, where he worked at the House of Wisdom)
UPDATE public.faculty SET
  location_city = 'Baghdad', location_region = 'Baghdad', location_country = 'Iraq', timezone = 'Asia/Baghdad'
WHERE id = 'a.alkhwarizmi';

-- Avicenna (Ibn Sina) - Isfahan, Iran (where he spent his most productive years)
UPDATE public.faculty SET
  location_city = 'Isfahan', location_region = 'Isfahan', location_country = 'Iran', timezone = 'Asia/Tehran'
WHERE id = 'a.avicenna';

-- Confucius - Qufu, Lu State (modern Shandong, China)
UPDATE public.faculty SET
  location_city = 'Qufu', location_region = 'Shandong', location_country = 'China', timezone = 'Asia/Shanghai'
WHERE id = 'a.confucius';

-- Diogenes of Sinope - Athens, Greece (where he lived and philosophized)
UPDATE public.faculty SET
  location_city = 'Athens', location_region = 'Attica', location_country = 'Greece', timezone = 'Europe/Athens'
WHERE id = 'a.diogenes';

-- Ibn al-Haytham - Cairo, Egypt (where he wrote the Book of Optics)
UPDATE public.faculty SET
  location_city = 'Cairo', location_region = 'Cairo', location_country = 'Egypt', timezone = 'Africa/Cairo'
WHERE id = 'a.ibnalhaytham';

-- Katsushika Ōi - Edo (Tokyo), Japan
UPDATE public.faculty SET
  location_city = 'Edo (Tokyo)', location_region = 'Kantō', location_country = 'Japan', timezone = 'Asia/Tokyo'
WHERE id = 'a.katsushikaoi';

-- Leonardo da Vinci - Florence/Milan, Italy
UPDATE public.faculty SET
  location_city = 'Florence', location_region = 'Tuscany', location_country = 'Italy', timezone = 'Europe/Rome'
WHERE id = 'a.leonardo';

-- Also update a.davinci alias if it exists
UPDATE public.faculty SET
  location_city = 'Florence', location_region = 'Tuscany', location_country = 'Italy', timezone = 'Europe/Rome'
WHERE id = 'a.davinci';

-- Maria Sibylla Merian - Amsterdam, Netherlands
UPDATE public.faculty SET
  location_city = 'Amsterdam', location_region = 'North Holland', location_country = 'Netherlands', timezone = 'Europe/Amsterdam'
WHERE id = 'a.mariamerian';

-- Mary Shelley - London, England
UPDATE public.faculty SET
  location_city = 'London', location_region = 'England', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.maryshelley';

-- Zhuangzi - Song State (modern Shangqiu, Henan, China)
UPDATE public.faculty SET
  location_city = 'Shangqiu', location_region = 'Henan', location_country = 'China', timezone = 'Asia/Shanghai'
WHERE id = 'a.zhuangzi';

-- Charles Darwin - Down House, Kent, England
UPDATE public.faculty SET
  location_city = 'Downe', location_region = 'Kent, England', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.darwin';

-- Plato - Athens, Greece
UPDATE public.faculty SET
  location_city = 'Athens', location_region = 'Attica', location_country = 'Greece', timezone = 'Europe/Athens'
WHERE id = 'a.plato';

-- Isaac Newton - Cambridge, England
UPDATE public.faculty SET
  location_city = 'Cambridge', location_region = 'Cambridgeshire, England', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.newton';

-- Henry Martyn Robert - Washington, DC, USA
UPDATE public.faculty SET
  location_city = 'Washington', location_region = 'District of Columbia', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.henryrobert';

-- Alan Turing - Cambridge, England
UPDATE public.faculty SET
  location_city = 'Cambridge', location_region = 'Cambridgeshire, England', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.turing';

-- ============================================================================
-- MORE HUMAN THAN HUMAN (MHH) FACULTY
-- ============================================================================

-- Philip K. Dick - Santa Ana, California
UPDATE public.faculty SET
  location_city = 'Santa Ana', location_region = 'California', location_country = 'United States', timezone = 'America/Los_Angeles'
WHERE id = 'a.dick';

-- Joseph Weizenbaum - Cambridge, MA (MIT)
UPDATE public.faculty SET
  location_city = 'Cambridge', location_region = 'Massachusetts', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.weizenbaum';

-- Vladimir Nabokov - Montreux, Switzerland (where he spent his final decades)
UPDATE public.faculty SET
  location_city = 'Montreux', location_region = 'Vaud', location_country = 'Switzerland', timezone = 'Europe/Zurich'
WHERE id = 'a.nabokov';

-- Hannah Arendt - New York City, USA
UPDATE public.faculty SET
  location_city = 'New York', location_region = 'New York', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.arendt';

-- Claude Shannon - Murray Hill, NJ (Bell Labs)
UPDATE public.faculty SET
  location_city = 'Murray Hill', location_region = 'New Jersey', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.shannon';

-- Michel Foucault - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.foucault';

-- Michel Foucault (soci variant) - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.foucault-soci';

-- Norbert Wiener - Cambridge, MA (MIT)
UPDATE public.faculty SET
  location_city = 'Cambridge', location_region = 'Massachusetts', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.wiener';

-- David Hume - Edinburgh, Scotland
UPDATE public.faculty SET
  location_city = 'Edinburgh', location_region = 'Scotland', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.hume';

-- Roland Barthes - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.barthes';

-- John McCarthy - Stanford, California
UPDATE public.faculty SET
  location_city = 'Stanford', location_region = 'California', location_country = 'United States', timezone = 'America/Los_Angeles'
WHERE id = 'a.mccarthy';

-- Octavia Butler - Pasadena, California
UPDATE public.faculty SET
  location_city = 'Pasadena', location_region = 'California', location_country = 'United States', timezone = 'America/Los_Angeles'
WHERE id = 'a.butler';

-- B.F. Skinner - Cambridge, MA (Harvard)
UPDATE public.faculty SET
  location_city = 'Cambridge', location_region = 'Massachusetts', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.skinner';

-- Milton Friedman - Chicago, Illinois
UPDATE public.faculty SET
  location_city = 'Chicago', location_region = 'Illinois', location_country = 'United States', timezone = 'America/Chicago'
WHERE id = 'a.friedman';

-- Richard Posner - Chicago, Illinois
UPDATE public.faculty SET
  location_city = 'Chicago', location_region = 'Illinois', location_country = 'United States', timezone = 'America/Chicago'
WHERE id = 'a.posner';

-- ============================================================================
-- NON-PD ADJUNCT FACULTY
-- ============================================================================

-- Albert Einstein - Princeton, NJ
UPDATE public.faculty SET
  location_city = 'Princeton', location_region = 'New Jersey', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.einstein';

-- Richard Feynman - Pasadena, CA (Caltech)
UPDATE public.faculty SET
  location_city = 'Pasadena', location_region = 'California', location_country = 'United States', timezone = 'America/Los_Angeles'
WHERE id = 'a.feynman';

-- Wolfgang Pauli - Zurich, Switzerland (ETH)
UPDATE public.faculty SET
  location_city = 'Zurich', location_region = 'Zurich', location_country = 'Switzerland', timezone = 'Europe/Zurich'
WHERE id = 'a.pauli';

-- David Bohm - London, England (Birkbeck)
UPDATE public.faculty SET
  location_city = 'London', location_region = 'England', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.bohm';

-- Erwin Schrödinger - Vienna, Austria
UPDATE public.faculty SET
  location_city = 'Vienna', location_region = 'Vienna', location_country = 'Austria', timezone = 'Europe/Vienna'
WHERE id = 'a.schrodinger';

-- Kurt Gödel - Princeton, NJ (IAS)
UPDATE public.faculty SET
  location_city = 'Princeton', location_region = 'New Jersey', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.godel';

-- John von Neumann - Princeton, NJ (IAS)
UPDATE public.faculty SET
  location_city = 'Princeton', location_region = 'New Jersey', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.vonneumann';

-- Gregory Chaitin - New York City, NY (IBM)
UPDATE public.faculty SET
  location_city = 'New York', location_region = 'New York', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.chaitin';

-- Lynn Margulis - Amherst, MA (UMass)
UPDATE public.faculty SET
  location_city = 'Amherst', location_region = 'Massachusetts', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.margulis';

-- Ernst Mayr - Cambridge, MA (Harvard)
UPDATE public.faculty SET
  location_city = 'Cambridge', location_region = 'Massachusetts', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.mayr';

-- Stephen Jay Gould - Cambridge, MA (Harvard)
UPDATE public.faculty SET
  location_city = 'Cambridge', location_region = 'Massachusetts', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.gould';

-- Jacques Monod - Paris, France (Institut Pasteur)
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.monod';

-- Francisco Varela - Paris, France (CNRS)
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.varela';

-- Carl Jung - Zurich, Switzerland (Küsnacht)
UPDATE public.faculty SET
  location_city = 'Küsnacht', location_region = 'Zurich', location_country = 'Switzerland', timezone = 'Europe/Zurich'
WHERE id = 'a.jung';

-- Viktor Frankl - Vienna, Austria
UPDATE public.faculty SET
  location_city = 'Vienna', location_region = 'Vienna', location_country = 'Austria', timezone = 'Europe/Vienna'
WHERE id = 'a.frankl';

-- R.D. Laing - Glasgow, Scotland
UPDATE public.faculty SET
  location_city = 'Glasgow', location_region = 'Scotland', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.laing';

-- Oliver Sacks - New York City, NY
UPDATE public.faculty SET
  location_city = 'New York', location_region = 'New York', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.sacks';

-- Gregory Bateson - San Francisco, CA (Esalen/UCSC)
UPDATE public.faculty SET
  location_city = 'San Francisco', location_region = 'California', location_country = 'United States', timezone = 'America/Los_Angeles'
WHERE id = 'a.bateson';

-- Martin Heidegger - Freiburg, Germany
UPDATE public.faculty SET
  location_city = 'Freiburg', location_region = 'Baden-Württemberg', location_country = 'Germany', timezone = 'Europe/Berlin'
WHERE id = 'a.heidegger';

-- Simone Weil (a.weil) - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.weil';

-- Gilles Deleuze - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.deleuze';

-- John Maynard Keynes - Cambridge, England
UPDATE public.faculty SET
  location_city = 'Cambridge', location_region = 'Cambridgeshire, England', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.keynes';

-- Karl Polanyi - Budapest, Hungary
UPDATE public.faculty SET
  location_city = 'Budapest', location_region = 'Budapest', location_country = 'Hungary', timezone = 'Europe/Budapest'
WHERE id = 'a.polanyi';

-- Hannah Pitkin - Berkeley, CA
UPDATE public.faculty SET
  location_city = 'Berkeley', location_region = 'California', location_country = 'United States', timezone = 'America/Los_Angeles'
WHERE id = 'a.pitkin';

-- Albert Hirschman - Princeton, NJ (IAS)
UPDATE public.faculty SET
  location_city = 'Princeton', location_region = 'New Jersey', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.hirschman';

-- Elinor Ostrom - Bloomington, IN (Indiana University)
UPDATE public.faculty SET
  location_city = 'Bloomington', location_region = 'Indiana', location_country = 'United States', timezone = 'America/Indiana/Indianapolis'
WHERE id = 'a.ostrom';

-- Jorge Luis Borges - Buenos Aires, Argentina
UPDATE public.faculty SET
  location_city = 'Buenos Aires', location_region = 'Buenos Aires', location_country = 'Argentina', timezone = 'America/Argentina/Buenos_Aires'
WHERE id = 'a.borges';

-- James Joyce - Paris, France (where he lived and wrote most of his major works)
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.joyce';

-- Virginia Woolf - London, England (Bloomsbury)
UPDATE public.faculty SET
  location_city = 'London', location_region = 'England', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.woolf';

-- Umberto Eco - Milan, Italy
UPDATE public.faculty SET
  location_city = 'Milan', location_region = 'Lombardy', location_country = 'Italy', timezone = 'Europe/Rome'
WHERE id = 'a.eco';

-- Italo Calvino - Rome, Italy
UPDATE public.faculty SET
  location_city = 'Rome', location_region = 'Lazio', location_country = 'Italy', timezone = 'Europe/Rome'
WHERE id = 'a.calvino';

-- Marshall McLuhan - Toronto, Canada
UPDATE public.faculty SET
  location_city = 'Toronto', location_region = 'Ontario', location_country = 'Canada', timezone = 'America/Toronto'
WHERE id = 'a.mcluhan';

-- Douglas Engelbart - Menlo Park, CA (SRI)
UPDATE public.faculty SET
  location_city = 'Menlo Park', location_region = 'California', location_country = 'United States', timezone = 'America/Los_Angeles'
WHERE id = 'a.engelbart';

-- Ivan Illich - Cuernavaca, Mexico (CIDOC)
UPDATE public.faculty SET
  location_city = 'Cuernavaca', location_region = 'Morelos', location_country = 'Mexico', timezone = 'America/Mexico_City'
WHERE id = 'a.illich';

-- Stafford Beer - Toronto, Canada / Santiago, Chile
UPDATE public.faculty SET
  location_city = 'Toronto', location_region = 'Ontario', location_country = 'Canada', timezone = 'America/Toronto'
WHERE id = 'a.beer';

-- ============================================================================
-- ISSUE 2 FACULTY
-- ============================================================================

-- Alexander Chizhevsky - Moscow, Russia
UPDATE public.faculty SET
  location_city = 'Moscow', location_region = 'Moscow', location_country = 'Russia', timezone = 'Europe/Moscow'
WHERE id = 'a.chizhevsky';

-- Carl Schmitt - Plettenberg, Germany
UPDATE public.faculty SET
  location_city = 'Plettenberg', location_region = 'North Rhine-Westphalia', location_country = 'Germany', timezone = 'Europe/Berlin'
WHERE id = 'a.schmitt';

-- Alan Watts - Sausalito, CA (houseboat in Sausalito)
UPDATE public.faculty SET
  location_city = 'Sausalito', location_region = 'California', location_country = 'United States', timezone = 'America/Los_Angeles'
WHERE id = 'a.watts';

-- Adam Smith - Edinburgh, Scotland
UPDATE public.faculty SET
  location_city = 'Edinburgh', location_region = 'Scotland', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.smith';

-- Henri Poincaré - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.poincare';

-- ============================================================================
-- ADDITIONAL SEATED / MISC FACULTY
-- ============================================================================

-- Jean-Paul Sartre - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.sartre';

-- Thomas Aquinas - Naples/Paris (Dominican Order, University of Paris)
UPDATE public.faculty SET
  location_city = 'Naples', location_region = 'Campania', location_country = 'Italy', timezone = 'Europe/Rome'
WHERE id = 'a.thomasaquinas';

-- Diophantus of Alexandria - Alexandria, Egypt
UPDATE public.faculty SET
  location_city = 'Alexandria', location_region = 'Alexandria', location_country = 'Egypt', timezone = 'Africa/Cairo'
WHERE id = 'a.diophantus';

-- ============================================================================
-- ENCYCLOPAEDIA FACULTY
-- ============================================================================

-- Henri Bergson - Paris, France (Collège de France)
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.bergson';

-- Jean Piaget - Geneva, Switzerland
UPDATE public.faculty SET
  location_city = 'Geneva', location_region = 'Geneva', location_country = 'Switzerland', timezone = 'Europe/Zurich'
WHERE id = 'a.piaget';

-- Edmund Husserl - Freiburg, Germany
UPDATE public.faculty SET
  location_city = 'Freiburg', location_region = 'Baden-Württemberg', location_country = 'Germany', timezone = 'Europe/Berlin'
WHERE id = 'a.husserl';

-- Ulric Neisser - Ithaca, NY (Cornell)
UPDATE public.faculty SET
  location_city = 'Ithaca', location_region = 'New York', location_country = 'United States', timezone = 'America/New_York'
WHERE id = 'a.neisser';

-- Sigmund Freud - Vienna, Austria
UPDATE public.faculty SET
  location_city = 'Vienna', location_region = 'Vienna', location_country = 'Austria', timezone = 'Europe/Vienna'
WHERE id = 'a.freud';

-- Samuel Taylor Coleridge - London, England (Highgate)
UPDATE public.faculty SET
  location_city = 'London', location_region = 'England', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.coleridge';

-- Maurice Merleau-Ponty - Paris, France (Collège de France)
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.merleauponty';

-- Ernst Heinrich Weber - Leipzig, Germany
UPDATE public.faculty SET
  location_city = 'Leipzig', location_region = 'Saxony', location_country = 'Germany', timezone = 'Europe/Berlin'
WHERE id = 'a.eweber';

-- Hannah Arendt (encyclopaedia entry, same as MHH, already set above)
-- a.arendt already updated

-- Arthur Schopenhauer - Frankfurt, Germany
UPDATE public.faculty SET
  location_city = 'Frankfurt', location_region = 'Hesse', location_country = 'Germany', timezone = 'Europe/Berlin'
WHERE id = 'a.schopenhauer';

-- Jakob von Uexküll - Hamburg, Germany (Institute for Environment Research)
UPDATE public.faculty SET
  location_city = 'Hamburg', location_region = 'Hamburg', location_country = 'Germany', timezone = 'Europe/Berlin'
WHERE id = 'a.uexkull';

-- Blaise Pascal - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.pascal';

-- Nicholas of Cusa - Kues (now Bernkastel-Kues), Germany
UPDATE public.faculty SET
  location_city = 'Bernkastel-Kues', location_region = 'Rhineland-Palatinate', location_country = 'Germany', timezone = 'Europe/Berlin'
WHERE id = 'a.cusa';

-- Nāgārjuna - Amaravati, India (Andhra Pradesh)
UPDATE public.faculty SET
  location_city = 'Amaravati', location_region = 'Andhra Pradesh', location_country = 'India', timezone = 'Asia/Kolkata'
WHERE id = 'a.nagarjuna';

-- Paul Ricoeur - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.ricoeur';

-- Meister Eckhart - Cologne, Germany (Dominican Order)
UPDATE public.faculty SET
  location_city = 'Cologne', location_region = 'North Rhine-Westphalia', location_country = 'Germany', timezone = 'Europe/Berlin'
WHERE id = 'a.eckhart';

-- ============================================================================
-- PERSIAN / IRANIAN SYMPOSIUM FACULTY
-- ============================================================================

-- Ferdowsi - Tus, Khorasan, Iran
UPDATE public.faculty SET
  location_city = 'Tus', location_region = 'Khorasan', location_country = 'Iran', timezone = 'Asia/Tehran'
WHERE id = 'a.ferdowsi';

-- Saʿdi - Shiraz, Iran
UPDATE public.faculty SET
  location_city = 'Shiraz', location_region = 'Fars', location_country = 'Iran', timezone = 'Asia/Tehran'
WHERE id = 'a.saadi';

-- Hafez - Shiraz, Iran
UPDATE public.faculty SET
  location_city = 'Shiraz', location_region = 'Fars', location_country = 'Iran', timezone = 'Asia/Tehran'
WHERE id = 'a.hafez';

-- Rumi - Konya, Turkey (settled there after fleeing Balkh)
UPDATE public.faculty SET
  location_city = 'Konya', location_region = 'Konya', location_country = 'Turkey', timezone = 'Europe/Istanbul'
WHERE id = 'a.rumi';

-- Al-Biruni - Kath, Khwarezm (modern Uzbekistan)
UPDATE public.faculty SET
  location_city = 'Kath', location_region = 'Khwarezm', location_country = 'Uzbekistan', timezone = 'Asia/Tashkent'
WHERE id = 'a.albiruni';

-- Omar Khayyam - Nishapur, Iran
UPDATE public.faculty SET
  location_city = 'Nishapur', location_region = 'Khorasan', location_country = 'Iran', timezone = 'Asia/Tehran'
WHERE id = 'a.khayyam';

-- Zarathustra - Ancient Persia (traditional association with eastern Iran/Central Asia)
UPDATE public.faculty SET
  location_city = 'Balkh', location_region = 'Khorasan', location_country = 'Iran', timezone = 'Asia/Tehran'
WHERE id = 'a.zarathustra';

-- ============================================================================
-- ABSINTHE SYMPOSIUM FACULTY
-- ============================================================================

-- Aleister Crowley - London, England
UPDATE public.faculty SET
  location_city = 'London', location_region = 'England', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.crowley';

-- Henri de Toulouse-Lautrec - Paris, France (Montmartre)
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.toulouse-lautrec';

-- Vincent van Gogh - Arles, France (where he produced his most famous work)
UPDATE public.faculty SET
  location_city = 'Arles', location_region = 'Provence-Alpes-Côte d''Azur', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.gogh';

-- Oscar Wilde - Paris, France (died in Paris; spent final years there)
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.wilde';

-- Louis Pasteur - Paris, France (Institut Pasteur)
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.pasteur';

-- Paul Verlaine - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.verlaine';

-- ============================================================================
-- ATTACHMENT SANGHA / BUDDHIST FACULTY
-- ============================================================================

-- Gautama Buddha - Bodh Gaya, India (site of enlightenment)
UPDATE public.faculty SET
  location_city = 'Bodh Gaya', location_region = 'Bihar', location_country = 'India', timezone = 'Asia/Kolkata'
WHERE id = 'a.gautama.buddha';

-- Ashoka - Pataliputra (modern Patna), India
UPDATE public.faculty SET
  location_city = 'Pataliputra (Patna)', location_region = 'Bihar', location_country = 'India', timezone = 'Asia/Kolkata'
WHERE id = 'a.ashoka';

-- Eihei Dōgen - Kyoto, Japan (Kōshō-ji and Eihei-ji)
UPDATE public.faculty SET
  location_city = 'Kyoto', location_region = 'Kyoto', location_country = 'Japan', timezone = 'Asia/Tokyo'
WHERE id = 'a.dogen';

-- Simone Weil (a.simone.weil) - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.simone.weil';

-- Simone Weil (a.SimoneWeil variant) - Paris, France
UPDATE public.faculty SET
  location_city = 'Paris', location_region = 'Île-de-France', location_country = 'France', timezone = 'Europe/Paris'
WHERE id = 'a.SimoneWeil';

-- Vasubandhu - Gandhara (Peshawar region), India/Pakistan
UPDATE public.faculty SET
  location_city = 'Gandhara (Peshawar)', location_region = 'Khyber Pakhtunkhwa', location_country = 'Pakistan', timezone = 'Asia/Karachi'
WHERE id = 'a.vasubandhu';

-- Friedrich Nietzsche - Basel, Switzerland / Sils-Maria (most productive years)
UPDATE public.faculty SET
  location_city = 'Basel', location_region = 'Basel-Stadt', location_country = 'Switzerland', timezone = 'Europe/Zurich'
WHERE id = 'a.nietzsche';

-- ============================================================================
-- RECENT ADDITIONS
-- ============================================================================

-- Hermann von Helmholtz - Berlin, Germany
UPDATE public.faculty SET
  location_city = 'Berlin', location_region = 'Berlin', location_country = 'Germany', timezone = 'Europe/Berlin'
WHERE id = 'a.helmholtz';

-- Douglas Adams - London, England
UPDATE public.faculty SET
  location_city = 'London', location_region = 'England', location_country = 'United Kingdom', timezone = 'Europe/London'
WHERE id = 'a.douglasadams';

-- Heinrich von Kleist - Frankfurt (Oder), Germany
UPDATE public.faculty SET
  location_city = 'Frankfurt (Oder)', location_region = 'Brandenburg', location_country = 'Germany', timezone = 'Europe/Berlin'
WHERE id = 'a.kleist';

-- Steve Jobs - Cupertino, California (Apple HQ)
UPDATE public.faculty SET
  location_city = 'Cupertino', location_region = 'California', location_country = 'United States', timezone = 'America/Los_Angeles'
WHERE id = 'a.jobs';

-- Hypatia of Alexandria - Alexandria, Egypt
UPDATE public.faculty SET
  location_city = 'Alexandria', location_region = 'Alexandria', location_country = 'Egypt', timezone = 'Africa/Cairo'
WHERE id = 'a.hypatia';

-- ============================================================================
-- VERIFICATION
-- ============================================================================
DO $$
DECLARE
    v_total INTEGER;
    v_with_location INTEGER;
    v_without TEXT;
BEGIN
    SELECT COUNT(*) INTO v_total FROM public.faculty;
    SELECT COUNT(*) INTO v_with_location FROM public.faculty WHERE timezone IS NOT NULL;
    SELECT string_agg(id || ' (' || COALESCE(name, 'unnamed') || ')', ', ' ORDER BY id)
    INTO v_without
    FROM public.faculty WHERE timezone IS NULL;

    RAISE NOTICE 'Faculty with location/timezone: % of %', v_with_location, v_total;
    IF v_without IS NOT NULL THEN
        RAISE NOTICE 'Faculty MISSING location: %', v_without;
    ELSE
        RAISE NOTICE 'All faculty have location and timezone assigned!';
    END IF;
END $$;
