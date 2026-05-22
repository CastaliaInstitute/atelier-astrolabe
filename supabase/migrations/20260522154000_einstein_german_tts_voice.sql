update public.faculty
set
  google_tts_voice_name = 'de-DE-Neural2-B',
  google_tts_language_code = 'de-DE',
  voice_accent = 'noticeably German-accented English',
  voice_language = 'German and English',
  voice_prompt = 'Einstein should sound like a German-born physicist speaking English: German cadence, crisp consonants, and occasional German phrasing. Keep it intelligible and respectful; do not turn it into parody.'
where id = 'a.einstein'
   or slug = 'a.einstein';
