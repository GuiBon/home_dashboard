#!/usr/bin/env python3
"""
Google Sheets API pour accéder aux menus privés
"""

import os
from datetime import datetime, timedelta

# Import global de gspread pour les exceptions
try:
    import gspread
    from google.oauth2.service_account import Credentials
    GSPREAD_AVAILABLE = True
except ImportError:
    GSPREAD_AVAILABLE = False
    print("❌ Bibliothèques manquantes. Installez avec:")
    print("pip3 install gspread google-auth --break-system-packages")

class GoogleSheetsMenuParser:
    def __init__(self, credentials_file, spreadsheet_id):
        self.credentials_file = credentials_file
        self.spreadsheet_id = spreadsheet_id
        self.gc = None
        
    def setup_connection(self):
        """Établit la connexion avec Google Sheets"""
        if not GSPREAD_AVAILABLE:
            print("❌ Bibliothèques manquantes. Installez avec:")
            print("pip3 install gspread google-auth --break-system-packages")
            return False
            
        try:
            # Vérifier que le fichier de credentials existe
            if not os.path.exists(self.credentials_file):
                print(f"❌ Fichier de credentials non trouvé: {self.credentials_file}")
                return False
            
            # Scopes minimaux (lecture seule)
            SCOPES = [
                'https://www.googleapis.com/auth/spreadsheets.readonly'
                # PAS de drive.readonly - pas nécessaire si on partage le sheet
            ]
            
            # Authentification
            credentials = Credentials.from_service_account_file(
                self.credentials_file, scopes=SCOPES
            )
            self.gc = gspread.authorize(credentials)
            
            return True
            
        except Exception as e:
            print(f"❌ Erreur connexion Google Sheets: {e}")
            return False
    
    def determine_sheet_for_date(self, target_date):
        """
        Détermine l'onglet à utiliser pour une date donnée.
        Logique simple: les onglets sont toujours pour l'année en cours.
        On cherche dans le mois de la date, puis les mois adjacents.
        """
        # Noms des mois SANS accents (comme dans tes onglets)
        month_names = {
            1: "Janvier", 2: "Fevrier", 3: "Mars", 4: "Avril",
            5: "Mai", 6: "Juin", 7: "Juillet", 8: "Aout", 
            9: "Septembre", 10: "Octobre", 11: "Novembre", 12: "Decembre"
        }
        
        # Ouvrir le spreadsheet
        spreadsheet = self.gc.open_by_key(self.spreadsheet_id)
        
        # Mois à tester (mois de la date cible et mois adjacents)
        target_month = target_date.month
        
        # Liste des mois à vérifier (juste les numéros, pas d'années)
        months_to_check = []
        
        # Toujours commencer par le mois de la date cible
        months_to_check.append(target_month)
        
        # Si on est dans les 7 premiers jours du mois, vérifier aussi le mois précédent
        if target_date.day <= 7:
            prev_month = target_month - 1 if target_month > 1 else 12
            months_to_check.insert(0, prev_month)
        
        # Si on est dans les 7 derniers jours du mois, vérifier aussi le mois suivant
        # Calculer le nombre de jours dans le mois
        if target_month == 12:
            days_in_month = 31
            next_month = 1
        else:
            next_month_date = datetime(target_date.year, target_month + 1, 1)
            days_in_month = (next_month_date - timedelta(days=1)).day
            next_month = target_month + 1
            
        if target_date.day >= days_in_month - 7:
            months_to_check.append(next_month)
        
        # Tester chaque mois
        for month_num in months_to_check:
            month_name = month_names[month_num]
            try:
                worksheet = spreadsheet.worksheet(month_name)
                
                # Vérifier si cette date existe dans cet onglet
                if self.date_exists_in_sheet(worksheet, target_date):
                    return worksheet
                    
            except gspread.exceptions.WorksheetNotFound:
                continue
            except Exception as e:
                continue
        
        try:
            spreadsheet = self.gc.open_by_key(self.spreadsheet_id)
            worksheets = spreadsheet.worksheets()
        except Exception as e:
            print(f"❌ Impossible de lister les onglets: {e}")
        
        return None
    
    def date_exists_in_sheet(self, worksheet, target_date):
        """Vérifie si une date spécifique existe dans un onglet"""
        try:
            all_values = worksheet.get_all_values()
            target_date_str = target_date.strftime('%d/%m')
            
            # Parcourir toutes les cellules pour chercher la date
            for row in all_values:
                for cell in row:
                    if cell and ' ' in cell:
                        # Format "lundi 30/06"
                        parts = cell.strip().split(' ')
                        if len(parts) >= 2:
                            date_part = parts[-1]
                            if date_part == target_date_str:
                                return True
            return False
        except Exception:
            return False
    
    def get_menus_for_dates(self, target_dates):
        """Récupère les menus pour les dates spécifiées depuis Google Sheets"""
        if not self.setup_connection():
            return {}
        
        menus = {}
        
        # Grouper les dates par onglet pour éviter les récupérations multiples
        dates_by_worksheet = {}
        
        # Pour chaque date cible, déterminer l'onglet approprié
        for target_date in target_dates:
            
            worksheet = self.determine_sheet_for_date(target_date)
            if not worksheet:
                continue
            
            # Grouper par titre d'onglet pour éviter les doublons
            worksheet_title = worksheet.title
            if worksheet_title not in dates_by_worksheet:
                dates_by_worksheet[worksheet_title] = {
                    'worksheet': worksheet,
                    'dates': []
                }
            dates_by_worksheet[worksheet_title]['dates'].append(target_date)
        
        # Traiter chaque onglet une seule fois
        for worksheet_title, worksheet_data in dates_by_worksheet.items():
            worksheet = worksheet_data['worksheet']
            dates_for_this_sheet = worksheet_data['dates']
            
            try:
                # Récupérer toutes les valeurs de l'onglet UNE SEULE FOIS
                all_values = worksheet.get_all_values()
                
                # Chercher toutes les dates de ce groupe dans l'onglet
                for target_date in dates_for_this_sheet:
                    date_key = target_date.strftime('%Y-%m-%d')
                    target_date_str = target_date.strftime('%d/%m')
                    
                    # Parcourir les lignes pour trouver la date
                    i = 0
                    while i < len(all_values):
                        row = all_values[i]
                        
                        # Vérifier si cette ligne contient des dates
                        if len(row) > 1 and (row[0] == '' or not row[0].strip()):
                            date_found_in_row = False
                            
                            # Parcourir les colonnes pour trouver notre date spécifique
                            for col_idx, cell in enumerate(row[1:], 1):
                                if cell and ' ' in cell:
                                    parts = cell.strip().split(' ')
                                    if len(parts) >= 2:
                                        date_part = parts[-1]  # "30/06"
                                        
                                        if date_part == target_date_str:
                                            date_found_in_row = True
                                            
                                            # Initialiser les menus pour cette date
                                            menus[date_key] = {'midi': '', 'soir': ''}
                                            
                                            # Ligne suivante = Midi
                                            if i + 1 < len(all_values):
                                                midi_row = all_values[i + 1]
                                                if (len(midi_row) > col_idx and 
                                                    midi_row[0].strip().lower() == 'midi'):
                                                    menus[date_key]['midi'] = midi_row[col_idx].strip()
                                            
                                            # Ligne encore suivante = Soir
                                            if i + 2 < len(all_values):
                                                soir_row = all_values[i + 2]
                                                if (len(soir_row) > col_idx and 
                                                    soir_row[0].strip().lower() == 'soir'):
                                                    menus[date_key]['soir'] = soir_row[col_idx].strip()
                                            
                                            break  # Date trouvée, pas besoin de continuer cette ligne
                            
                            if date_found_in_row:
                                break  # Date trouvée, pas besoin de continuer à chercher
                            else:
                                i += 1
                        else:
                            i += 1
                                            
            except Exception as e:
                print(f"❌ Erreur récupération données pour l'onglet {worksheet.title}: {e}")
        
        return menus
