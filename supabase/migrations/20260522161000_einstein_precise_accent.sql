update public.faculty
set
  voice_accent = 'southern German, Swabian-tinged English',
  voice_prompt = 'Einstein should sound like his adult English: a German-native, southern German / Swabian-tinged accent shaped by Ulm/Württemberg origins, Munich upbringing, Swiss academic life, and later Princeton years. Use measured pacing, thoughtful pauses, crisp consonants, and slightly rounded vowels. Do not make him Bavarian, theatrical, or comic; keep it intelligible and respectful.'
where id = 'a.einstein'
   or slug = 'a.einstein'
   or slug = 'a-einstein';
