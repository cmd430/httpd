<?php
  $path = "../uploads/";
  $path = $path . basename( $_FILES['upload_file']['name']);
  if(move_uploaded_file($_FILES['upload_file']['tmp_name'], $path)) {
    echo "The file ".  basename( $_FILES['upload_file']['name']) . " has been uploaded";
  } else{
    echo "There was an error uploading the file, please try again!";
  }
?>