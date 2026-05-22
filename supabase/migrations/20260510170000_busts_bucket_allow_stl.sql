-- Allow printable meshes alongside portraits in the public busts bucket.

update storage.buckets
set
  allowed_mime_types = array[
    'image/png',
    'image/jpeg',
    'model/stl',
    'application/octet-stream'
  ]::text[]
where id = 'busts';
